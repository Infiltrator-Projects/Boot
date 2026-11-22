/*
 * BOOT / read-only LVM2 linear-volume discovery
 *
 * LVM2 describes block mappings, not a filesystem. Publish validated linear
 * logical volumes as EFI Block I/O + Disk I/O handles so the existing ext4
 * filesystem driver can mount them without filesystem-specific LVM code.
 *
 * No protocol or disk is modified on non-LVM systems. All writes are refused.
 * The subset deliberately excludes thin, cache, snapshots, RAID, encryption
 * and striped segments with multiple stripes; unknown layouts are not guessed.
 * The mapper is part of the signed BOOT image, not a separate EFI executable.
 */

#include "global.h"
#include "lib.h"
#include "log.h"
#include "driver_support.h"
#include "../include/boot_call_wrapper.h"

#define LVM_SECTOR          512U
#define LVM_PVS_MAX         16U
#define LVM_LVS_MAX         16U
#define LVM_SEGS_MAX        64U
#define LVM_TEXT_MAX        (1024U * 1024U)
#define LVM_CRC_SEED        0xf597a6cfU

extern EFI_GUID gMyEfiBlockIoProtocolGuid;
extern EFI_GUID gMyEfiDiskIoProtocolGuid;
extern EFI_GUID gMyEfiDevicePathProtocolGuid;

typedef struct {
    EFI_DISK_IO *Disk;
    EFI_BLOCK_IO *Block;
    UINT32 MediaId;
    UINT64 Bytes;
    CHAR8 Id[33];
    CHAR8 *Metadata;
    UINT64 Sequence;
    CHAR8 VgId[33];
} LVM_PV;

typedef struct {
    UINT64 Start;        /* logical sector */
    UINT64 Count;        /* 512-byte sectors */
    UINT64 Physical;     /* physical sector from start of PV */
    UINTN PvIndex;
} LVM_SEGMENT;

typedef struct {
    EFI_DEVICE_PATH Node;
    EFI_GUID Vendor;
    UINT8 Id[32];
    EFI_DEVICE_PATH End;
} LVM_DEVICE_PATH;

typedef struct {
    EFI_BLOCK_IO Block;
    EFI_BLOCK_IO_MEDIA Media;
    EFI_DISK_IO Disk;
    LVM_DEVICE_PATH DevicePath;
    EFI_HANDLE Handle;
    CHAR8 Id[33];
    UINT64 Sectors;
    UINTN SegCount;
    LVM_SEGMENT Segments[LVM_SEGS_MAX];
} LVM_LV;

typedef struct {
    const CHAR8 *Current;
    const CHAR8 *End;
} LVM_SPAN;

typedef struct {
    CHAR8 Alias[64];
    UINTN PvIndex;
    UINT64 PeStart;
    UINT64 PeCount;
} LVM_ALIAS;

static LVM_PV LvmPvs[LVM_PVS_MAX];
static LVM_LV *LvmLvs[LVM_LVS_MAX];
static UINTN LvmPvCount, LvmLvCount;
static BOOLEAN LvmEnumerated;

static const EFI_GUID LvmVendor = {
    0x1ac41d8e, 0x49fc, 0x47a4, {0xb4,0x38,0xe5,0x1b,0x16,0x93,0x25,0x7d}
};

static UINT32 LvmU32(const UINT8 *P) {
    return (UINT32)P[0] | ((UINT32)P[1] << 8) |
           ((UINT32)P[2] << 16) | ((UINT32)P[3] << 24);
}
static UINT64 LvmU64(const UINT8 *P) {
    return (UINT64)LvmU32(P) | ((UINT64)LvmU32(P + 4) << 32);
}
static BOOLEAN LvmSame(const CHAR8 *A, const CHAR8 *B) {
    UINTN i = 0;
    if (!A || !B) return FALSE;
    while (A[i] && B[i]) {
        if (A[i] != B[i]) return FALSE;
        i++;
    }
    return (A[i] == 0 && B[i] == 0);
}
static VOID LvmCopy(CHAR8 *Dst, UINTN Capacity, const CHAR8 *Src) {
    UINTN i = 0;
    if (Capacity == 0) return;
    while (Src && Src[i] && i + 1 < Capacity) {
        Dst[i] = Src[i]; i++;
    }
    Dst[i] = 0;
}
static BOOLEAN LvmNormalizeId(const CHAR8 *Src, CHAR8 Id[33]) {
    UINTN n = 0;
    while (Src && *Src) {
        CHAR8 c = *Src++;
        if (c == '-') continue;
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z')) || n == 32)
            return FALSE;
        if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
        Id[n++] = c;
    }
    Id[n] = 0;
    return n == 32;
}
static BOOLEAN LvmNumber(const CHAR8 *Text, UINT64 *Out) {
    UINT64 v = 0;
    if (!Text || !*Text) return FALSE;
    while (*Text) {
        UINT8 digit = (UINT8)(*Text++ - '0');
        if (digit > 9 || v > (~(UINT64)0 - digit) / 10)
            return FALSE;
        v = v * 10 + digit;
    }
    *Out = v;
    return TRUE;
}
static BOOLEAN LvmMultiply(UINT64 A, UINT64 B, UINT64 *Out) {
    if (B && A > ~(UINT64)0 / B) return FALSE;
    *Out = A * B;
    return TRUE;
}
static UINT32 LvmCrc(UINT32 Crc, const UINT8 *Data, UINTN Length) {
    UINTN i, b;
    for (i = 0; i < Length; ++i) {
        Crc ^= Data[i];
        for (b = 0; b < 8; ++b)
            Crc = (Crc >> 1) ^ ((Crc & 1) ? 0xedb88320U : 0U);
    }
    return Crc;
}

/* Tokenise LVM text without mutating its metadata; braces in quoted strings
 * or comments do not alter section depth. Reject truncated identifiers.
 */
static BOOLEAN LvmToken(LVM_SPAN *Span, CHAR8 *Out, UINTN Capacity) {
    const CHAR8 *P = Span->Current;
    UINTN n = 0;
    CHAR8 delimiter;
    while (P < Span->End) {
        if (*P == '#') {
            while (P < Span->End && *P != '\n') P++;
        } else if (*P == ' ' || *P == '\t' || *P == '\r' || *P == '\n') P++;
        else break;
    }
    if (P >= Span->End || !Capacity) {
        Span->Current = P;
        return FALSE;
    }
    if (*P == '{' || *P == '}' || *P == '[' || *P == ']' ||
        *P == '=' || *P == ',') {
        Out[0] = *P++;
        Out[1] = 0;
    } else {
        delimiter = 0;
        if (*P == '"') {
            delimiter = '"';
            P++;
        }
        while (P < Span->End) {
            if (delimiter) {
                if (*P == '"') { P++; break; }
                if (*P == '\\' && P + 1 < Span->End) P++;
            } else if (*P == ' ' || *P == '\t' || *P == '\r' ||
                       *P == '\n' || *P == '#' || *P == '{' ||
                       *P == '}' || *P == '[' || *P == ']' ||
                       *P == '=' || *P == ',') break;
            if (n + 1 >= Capacity) return FALSE;
            Out[n++] = *P++;
        }
        Out[n] = 0;
        if (!n) return FALSE;
    }
    Span->Current = P;
    return TRUE;
}
static BOOLEAN LvmSkipSection(LVM_SPAN *Walk, LVM_SPAN *Content) {
    CHAR8 Token[128];
    UINTN Depth = 1;
    Content->Current = Walk->Current;
    while (Walk->Current < Walk->End) {
        const CHAR8 *Begin = Walk->Current;
        if (!LvmToken(Walk, Token, sizeof(Token))) return FALSE;
        if (LvmSame(Token, "{")) Depth++;
        if (LvmSame(Token, "}") && --Depth == 0) {
            Content->End = Begin;
            return TRUE;
        }
    }
    return FALSE;
}
/* Iterate only immediate child sections; nested sections are skipped whole. */
static BOOLEAN LvmNextSection(LVM_SPAN *Walk, CHAR8 *Name,
                              UINTN NameSize, LVM_SPAN *Section) {
    CHAR8 First[128], Second[128];
    LVM_SPAN Next;
    while (LvmToken(Walk, First, sizeof(First))) {
        Next = *Walk;
        if (!LvmToken(&Next, Second, sizeof(Second))) return FALSE;
        if (LvmSame(Second, "{")) {
            *Walk = Next;
            if (!LvmSkipSection(Walk, Section)) return FALSE;
            LvmCopy(Name, NameSize, First);
            return TRUE;
        }
    }
    return FALSE;
}
static BOOLEAN LvmFindSection(LVM_SPAN Parent, const CHAR8 *Name,
                              LVM_SPAN *Section) {
    CHAR8 Found[128];
    while (LvmNextSection(&Parent, Found, sizeof(Found), Section))
        if (LvmSame(Name, Found)) return TRUE;
    return FALSE;
}
static BOOLEAN LvmFindField(LVM_SPAN Section, const CHAR8 *Field,
                            CHAR8 *Value, UINTN Size) {
    CHAR8 Name[128], Equal[128], Token[128];
    LVM_SPAN Probe, Nested;
    while (LvmToken(&Section, Name, sizeof(Name))) {
        Probe = Section;
        if (!LvmToken(&Probe, Equal, sizeof(Equal))) return FALSE;
        if (LvmSame(Equal, "{")) {
            Section = Probe;
            if (!LvmSkipSection(&Section, &Nested)) return FALSE;
            continue;
        }
        if (LvmSame(Name, Field) && LvmSame(Equal, "=")) {
            if (!LvmToken(&Probe, Token, sizeof(Token))) return FALSE;
            LvmCopy(Value, Size, Token);
            return TRUE;
        }
    }
    return FALSE;
}
static BOOLEAN LvmGetNumber(LVM_SPAN Section, const CHAR8 *Field,
                            UINT64 *Number) {
    CHAR8 Value[128];
    return LvmFindField(Section, Field, Value, sizeof(Value)) &&
           LvmNumber(Value, Number);
}
static BOOLEAN LvmHasFlag(LVM_SPAN Section, const CHAR8 *Field,
                          const CHAR8 *Flag) {
    CHAR8 Name[128], Equal[128], Value[128];
    LVM_SPAN Probe, Nested;
    while (LvmToken(&Section, Name, sizeof(Name))) {
        Probe = Section;
        if (!LvmToken(&Probe, Equal, sizeof(Equal))) return FALSE;
        if (LvmSame(Equal, "{")) {
            Section = Probe;
            if (!LvmSkipSection(&Section, &Nested)) return FALSE;
            continue;
        }
        if (LvmSame(Name, Field) && LvmSame(Equal, "=")) {
            if (!LvmToken(&Probe, Value, sizeof(Value)) ||
                !LvmSame(Value, "[")) return FALSE;
            while (LvmToken(&Probe, Value, sizeof(Value))) {
                if (LvmSame(Value, "]")) break;
                if (LvmSame(Value, Flag)) return TRUE;
            }
            return FALSE;
        }
    }
    return FALSE;
}
static BOOLEAN LvmStripe(LVM_SPAN Section, CHAR8 *Alias,
                         UINTN AliasSize, UINT64 *Extent) {
    CHAR8 Name[128], Equal[128], Value[128];
    LVM_SPAN Probe, Nested;
    while (LvmToken(&Section, Name, sizeof(Name))) {
        Probe = Section;
        if (!LvmToken(&Probe, Equal, sizeof(Equal))) return FALSE;
        if (LvmSame(Equal, "{")) {
            Section = Probe;
            if (!LvmSkipSection(&Section, &Nested)) return FALSE;
            continue;
        }
        if (LvmSame(Name, "stripes") && LvmSame(Equal, "=")) {
            if (!LvmToken(&Probe, Value, sizeof(Value)) ||
                !LvmSame(Value, "[")) return FALSE;
            if (!LvmToken(&Probe, Value, sizeof(Value))) return FALSE;
            LvmCopy(Alias, AliasSize, Value);
            if (!LvmToken(&Probe, Value, sizeof(Value)) ||
                !LvmSame(Value, ",")) return FALSE;
            if (!LvmToken(&Probe, Value, sizeof(Value)) ||
                !LvmNumber(Value, Extent)) return FALSE;
            if (!LvmToken(&Probe, Value, sizeof(Value)) ||
                !LvmSame(Value, "]")) return FALSE;
            return TRUE;
        }
    }
    return FALSE;
}

static EFI_STATUS LvmReadPv(const LVM_PV *Pv, UINT64 Offset,
                            UINTN Size, VOID *Buffer) {
    if (!Pv || !Pv->Disk || Offset > Pv->Bytes ||
        Size > Pv->Bytes - Offset) return EFI_END_OF_MEDIA;
    return boot_call5_wrapper(Pv->Disk->ReadDisk, Pv->Disk, Pv->MediaId,
                              Offset, Size, Buffer);
}
static CHAR8 *LvmReadMetadata(LVM_PV *Pv, UINT64 Start, UINT64 AreaSize) {
    UINT8 Header[512];
    UINT64 Offset, Size;
    UINT32 Checksum, Flags;
    CHAR8 *Text;
    UINTN First;
    if (AreaSize < 1024 || Start > Pv->Bytes ||
        AreaSize > Pv->Bytes - Start) return NULL;
    if (EFI_ERROR(LvmReadPv(Pv, Start, sizeof(Header), Header))) return NULL;
    if (LvmCrc(LVM_CRC_SEED, Header + 4, 508) != LvmU32(Header) ||
        CompareMem(Header + 4, " LVM2 x[5A%r0N*>", 16) != 0 ||
        LvmU32(Header + 20) != 1 || LvmU64(Header + 24) != Start ||
        LvmU64(Header + 32) != AreaSize) return NULL;
    /* A metadata area can have multiple descriptors; use the first valid,
     * non-ignored current raw record. Relative offsets can wrap around the
     * circular metadata region, never into its header sector.
     */
    for (UINTN Pos = 40; Pos + 24 <= sizeof(Header); Pos += 24) {
        Offset = LvmU64(Header + Pos);
        Size = LvmU64(Header + Pos + 8);
        Checksum = LvmU32(Header + Pos + 16);
        Flags = LvmU32(Header + Pos + 20);
        if (!Offset && !Size) break;
        if ((Flags & 1) || Offset < 512 || Offset >= AreaSize ||
            !Size || Size > LVM_TEXT_MAX || Size > AreaSize - 512)
            continue;
        Text = AllocatePool((UINTN)Size + 1);
        if (!Text) return NULL;
        First = (UINTN)Size;
        if (Size > AreaSize - Offset) First = (UINTN)(AreaSize - Offset);
        if (EFI_ERROR(LvmReadPv(Pv, Start + Offset, First, Text)) ||
            ((UINT64)First < Size &&
             EFI_ERROR(LvmReadPv(Pv, Start + 512, (UINTN)Size - First,
                                  Text + First)))) {
            FreePool(Text);
            continue;
        }
        if (LvmCrc(LVM_CRC_SEED, (UINT8 *)Text, (UINTN)Size) != Checksum) {
            FreePool(Text);
            continue;
        }
        Text[Size] = '\0';
        return Text;
    }
    return NULL;
}
static VOID LvmDiscoverPhysical(EFI_BLOCK_IO *Block, EFI_DISK_IO *Disk) {
    UINT8 Label[512];
    LVM_PV Candidate, *Pv;
    UINT64 MediaBytes, AreaOff, AreaLen, Offset;
    UINTN Pos, Descriptor;
    CHAR8 RawId[33];
    if (LvmPvCount == LVM_PVS_MAX || !Block || !Disk ||
        !Block->Media || !Block->Media->MediaPresent ||
        Block->Media->BlockSize < 512 ||
        Block->Media->BlockSize > 4096 ||
        !Block->Media->LogicalPartition ||
        Block->Media->LastBlock == ~(EFI_LBA)0) return;
    if (!LvmMultiply((UINT64)Block->Media->LastBlock + 1,
                     Block->Media->BlockSize, &MediaBytes) ||
        MediaBytes < 4096) return;
    SetMem(&Candidate, sizeof(Candidate), 0);
    Candidate.Block = Block;
    Candidate.Disk = Disk;
    Candidate.MediaId = Block->Media->MediaId;
    Candidate.Bytes = MediaBytes;
    for (UINTN Sector = 0; Sector < 4; ++Sector) {
        if (EFI_ERROR(LvmReadPv(&Candidate, Sector * 512, 512, Label)))
            continue;
        if (CompareMem(Label, "LABELONE", 8) != 0 ||
            CompareMem(Label + 24, "LVM2 001", 8) != 0 ||
            LvmU64(Label + 8) != Sector ||
            LvmCrc(LVM_CRC_SEED, Label + 20, 492) != LvmU32(Label + 16))
            continue;
        Offset = LvmU32(Label + 20);
        if (Offset < 32 || Offset + 40 > 512) continue;
        for (Pos = 0; Pos < 32; ++Pos) RawId[Pos] = (CHAR8)Label[Offset + Pos];
        RawId[32] = 0;
        if (!LvmNormalizeId(RawId, Candidate.Id)) continue;
        Descriptor = (UINTN)Offset + 40;
        /* Skip data areas, terminated by a zero descriptor. */
        while (Descriptor + 16 <= 512 &&
               (LvmU64(Label + Descriptor) || LvmU64(Label + Descriptor + 8)))
            Descriptor += 16;
        if (Descriptor + 32 > 512) continue;
        Descriptor += 16;
        for (; Descriptor + 16 <= 512; Descriptor += 16) {
            AreaOff = LvmU64(Label + Descriptor);
            AreaLen = LvmU64(Label + Descriptor + 8);
            if (!AreaOff && !AreaLen) break;
            Candidate.Metadata = LvmReadMetadata(&Candidate, AreaOff, AreaLen);
            if (Candidate.Metadata) {
                Pv = &LvmPvs[LvmPvCount++];
                *Pv = Candidate;
                LOG(1, LOG_LINE_NORMAL, L"LVM2: validated physical volume metadata");
                return;
            }
        }
    }
}

/* The name in stripes[] is an alias from the VG metadata. Resolve aliases
 * via canonical PV UUID, never the host's /dev path (which is only a hint).
 */
static BOOLEAN LvmAliases(LVM_SPAN Group, LVM_ALIAS *Aliases,
                          UINTN *AliasCount) {
    LVM_SPAN Physical, Walk, Section;
    CHAR8 Alias[128], Field[128], Id[33];
    UINTN j;
    if (!LvmFindSection(Group, "physical_volumes", &Physical))
        return FALSE;
    Walk = Physical;
    *AliasCount = 0;
    while (LvmNextSection(&Walk, Alias, sizeof(Alias), &Section)) {
        if (*AliasCount == LVM_PVS_MAX ||
            !LvmFindField(Section, "id", Field, sizeof(Field)) ||
            !LvmNormalizeId(Field, Id)) return FALSE;
        for (j = 0; j < LvmPvCount; ++j)
            if (LvmSame(LvmPvs[j].Id, Id)) break;
        /* Missing PVs are permitted until a segment actually references one. */
        LvmCopy(Aliases[*AliasCount].Alias,
                sizeof(Aliases[*AliasCount].Alias), Alias);
        Aliases[*AliasCount].PvIndex = j;
        if (!LvmGetNumber(Section, "pe_start",
                          &Aliases[*AliasCount].PeStart) ||
            !LvmGetNumber(Section, "pe_count",
                          &Aliases[*AliasCount].PeCount)) return FALSE;
        (*AliasCount)++;
    }
    return *AliasCount > 0;
}
static BOOLEAN LvmParseSegments(LVM_SPAN Logical, UINT64 ExtentSize,
                                LVM_ALIAS *Aliases, UINTN AliasCount,
                                LVM_LV *Lv) {
    LVM_SPAN Walk = Logical, Segment;
    CHAR8 Name[128], Type[128], Alias[128];
    UINT64 First, Count, PhysicalExtent, CountSectors, LogicalStart;
    UINT64 PhysStart, PhysEnd, Expected = 0, CountDeclared;
    UINTN a;
    if (!LvmGetNumber(Logical, "segment_count", &CountDeclared) ||
        CountDeclared == 0 || CountDeclared > LVM_SEGS_MAX)
        return FALSE;
    while (LvmNextSection(&Walk, Name, sizeof(Name), &Segment)) {
        if (Name[0] != 's' || Name[1] != 'e' || Name[2] != 'g' ||
            Lv->SegCount == LVM_SEGS_MAX ||
            !LvmFindField(Segment, "type", Type, sizeof(Type)) ||
            (!LvmSame(Type, "striped") && !LvmSame(Type, "linear")) ||
            !LvmGetNumber(Segment, "start_extent", &First) ||
            !LvmGetNumber(Segment, "extent_count", &Count) ||
            !Count || !LvmStripe(Segment, Alias, sizeof(Alias), &PhysicalExtent))
            return FALSE;
        if (LvmSame(Type, "striped")) {
            UINT64 Stripes;
            if (!LvmGetNumber(Segment, "stripe_count", &Stripes) ||
                Stripes != 1) return FALSE;
        }
        for (a = 0; a < AliasCount; ++a)
            if (LvmSame(Alias, Aliases[a].Alias)) break;
        if (a == AliasCount || Aliases[a].PvIndex == LvmPvCount ||
            First != Expected || PhysicalExtent > Aliases[a].PeCount ||
            Count > Aliases[a].PeCount - PhysicalExtent ||
            !LvmMultiply(First, ExtentSize, &LogicalStart) ||
            !LvmMultiply(Count, ExtentSize, &CountSectors) ||
            !LvmMultiply(PhysicalExtent, ExtentSize, &PhysStart) ||
            Aliases[a].PeStart > ~(UINT64)0 - PhysStart)
            return FALSE;
        PhysStart += Aliases[a].PeStart;
        if (PhysStart > ~(UINT64)0 - CountSectors) return FALSE;
        PhysEnd = PhysStart + CountSectors;
        if (PhysEnd > LvmPvs[Aliases[a].PvIndex].Bytes / 512 ||
            First > ~(UINT64)0 - Count) return FALSE;
        Lv->Segments[Lv->SegCount].Start = LogicalStart;
        Lv->Segments[Lv->SegCount].Count = CountSectors;
        Lv->Segments[Lv->SegCount].Physical = PhysStart;
        Lv->Segments[Lv->SegCount].PvIndex = Aliases[a].PvIndex;
        Lv->SegCount++;
        Expected = First + Count;
    }
    if (Lv->SegCount != CountDeclared ||
        !LvmMultiply(Expected, ExtentSize, &Lv->Sectors) ||
        Lv->Sectors == 0) return FALSE;
    return TRUE;
}

#define LVM_OWNER(Ptr, Type, Field) \
    ((Type *)((UINT8 *)(Ptr) - (UINTN)&((Type *)0)->Field))

static EFI_STATUS LvmMappedRead(LVM_LV *Lv, UINT64 Offset, UINTN Bytes,
                                VOID *Buffer) {
    UINT64 Capacity, LogicalSector, IntoSegment, Size;
    UINTN i, Chunk;
    EFI_STATUS Status;
    UINT8 *Out = (UINT8 *)Buffer;
    if (!LvmMultiply(Lv->Sectors, 512, &Capacity) ||
        Offset > Capacity || Bytes > Capacity - Offset)
        return EFI_END_OF_MEDIA;
    while (Bytes) {
        LogicalSector = Offset / 512;
        for (i = 0; i < Lv->SegCount; ++i)
            if (LogicalSector >= Lv->Segments[i].Start &&
                LogicalSector - Lv->Segments[i].Start <
                    Lv->Segments[i].Count) break;
        if (i == Lv->SegCount) return EFI_VOLUME_CORRUPTED;
        IntoSegment = Offset - Lv->Segments[i].Start * 512;
        Size = Lv->Segments[i].Count * 512 - IntoSegment;
        Chunk = Bytes < Size ? Bytes : (UINTN)Size;
        Status = LvmReadPv(&LvmPvs[Lv->Segments[i].PvIndex],
                           Lv->Segments[i].Physical * 512 + IntoSegment,
                           Chunk, Out);
        if (EFI_ERROR(Status)) return Status;
        Offset += Chunk;
        Out += Chunk;
        Bytes -= Chunk;
    }
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI LvmBlockReset(EFI_BLOCK_IO *This,
                                      BOOLEAN ExtendedVerification) {
    (VOID)This; (VOID)ExtendedVerification;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI LvmBlockRead(EFI_BLOCK_IO *This, UINT32 MediaId,
                                     EFI_LBA Lba, UINTN BufferSize,
                                     VOID *Buffer) {
    LVM_LV *Lv = LVM_OWNER(This, LVM_LV, Block);
    if (MediaId != Lv->Media.MediaId) return EFI_MEDIA_CHANGED;
    if (!Buffer || BufferSize % 512) return EFI_BAD_BUFFER_SIZE;
    if (Lba >= Lv->Sectors && BufferSize) return EFI_END_OF_MEDIA;
    return LvmMappedRead(Lv, Lba * 512, BufferSize, Buffer);
}
static EFI_STATUS EFIAPI LvmBlockWrite(EFI_BLOCK_IO *This, UINT32 MediaId,
                                      EFI_LBA Lba, UINTN BufferSize,
                                      VOID *Buffer) {
    (VOID)This; (VOID)MediaId; (VOID)Lba; (VOID)BufferSize; (VOID)Buffer;
    return EFI_WRITE_PROTECTED;
}
static EFI_STATUS EFIAPI LvmFlush(EFI_BLOCK_IO *This) {
    (VOID)This;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI LvmDiskRead(EFI_DISK_IO *This, UINT32 MediaId,
                                    UINT64 Offset, UINTN BufferSize,
                                    VOID *Buffer) {
    LVM_LV *Lv = LVM_OWNER(This, LVM_LV, Disk);
    if (MediaId != Lv->Media.MediaId) return EFI_MEDIA_CHANGED;
    if (!Buffer && BufferSize) return EFI_INVALID_PARAMETER;
    return LvmMappedRead(Lv, Offset, BufferSize, Buffer);
}
static EFI_STATUS EFIAPI LvmDiskWrite(EFI_DISK_IO *This, UINT32 MediaId,
                                     UINT64 Offset, UINTN BufferSize,
                                     VOID *Buffer) {
    (VOID)This; (VOID)MediaId; (VOID)Offset; (VOID)BufferSize; (VOID)Buffer;
    return EFI_WRITE_PROTECTED;
}
static BOOLEAN LvmPublish(LVM_LV *Lv) {
    EFI_STATUS Status;
    EFI_HANDLE Handle = NULL;
    UINTN i;
    if (LvmLvCount == LVM_LVS_MAX) return FALSE;
    for (i = 0; i < LvmLvCount; ++i)
        if (LvmSame(LvmLvs[i]->Id, Lv->Id)) return FALSE;
    Lv->Media.MediaId = 0x4c560000U + (UINT32)LvmLvCount;
    Lv->Media.RemovableMedia = FALSE;
    Lv->Media.MediaPresent = TRUE;
    Lv->Media.LogicalPartition = TRUE;
    Lv->Media.ReadOnly = TRUE;
    Lv->Media.WriteCaching = FALSE;
    Lv->Media.BlockSize = 512;
    Lv->Media.IoAlign = 1;
    Lv->Media.LastBlock = Lv->Sectors - 1;
    Lv->Block.Revision = 0x00010000;
    Lv->Block.Media = &Lv->Media;
    Lv->Block.Reset = LvmBlockReset;
    Lv->Block.ReadBlocks = LvmBlockRead;
    Lv->Block.WriteBlocks = LvmBlockWrite;
    Lv->Block.FlushBlocks = LvmFlush;
    Lv->Disk.Revision = 0x00010000;
    Lv->Disk.ReadDisk = LvmDiskRead;
    Lv->Disk.WriteDisk = LvmDiskWrite;
    Lv->DevicePath.Node.Type = MESSAGING_DEVICE_PATH;
    Lv->DevicePath.Node.SubType = MSG_VENDOR_DP;
    Lv->DevicePath.Node.Length[0] = sizeof(EFI_DEVICE_PATH) +
                                     sizeof(EFI_GUID) + sizeof(Lv->DevicePath.Id);
    Lv->DevicePath.Node.Length[1] = 0;
    Lv->DevicePath.Vendor = LvmVendor;
    for (i = 0; i < sizeof(Lv->DevicePath.Id); ++i)
        Lv->DevicePath.Id[i] = (UINT8)Lv->Id[i];
    Lv->DevicePath.End.Type = END_DEVICE_PATH_TYPE;
    Lv->DevicePath.End.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
    Lv->DevicePath.End.Length[0] = sizeof(EFI_DEVICE_PATH);
    Lv->DevicePath.End.Length[1] = 0;
    Status = boot_call4_wrapper(BS->InstallMultipleProtocolInterfaces,
                &Handle,
                &gMyEfiDevicePathProtocolGuid, &Lv->DevicePath,
                &gMyEfiBlockIoProtocolGuid, &Lv->Block,
                &gMyEfiDiskIoProtocolGuid, &Lv->Disk, NULL);
    if (EFI_ERROR(Status)) {
        LOG(1, LOG_LINE_NORMAL, L"LVM2: virtual volume protocol install failed: %lu",
            (UINT64)Status);
        return FALSE;
    }
    Lv->Handle = Handle;
    LvmLvs[LvmLvCount++] = Lv;
    LOG(1, LOG_LINE_NORMAL, L"LVM2: read-only logical volume published (%lu sectors)",
        Lv->Sectors);
    return TRUE;
}
static VOID LvmMapGroup(LVM_SPAN Group) {
    LVM_ALIAS Aliases[LVM_PVS_MAX];
    LVM_SPAN LogicalVolumes, Walk, Logical;
    LVM_LV *Lv;
    CHAR8 Name[128], Identifier[128];
    UINT64 ExtentSize;
    UINTN AliasCount;
    if (!LvmGetNumber(Group, "extent_size", &ExtentSize) ||
        ExtentSize == 0 || ExtentSize > (1ULL << 32) ||
        !LvmAliases(Group, Aliases, &AliasCount) ||
        !LvmFindSection(Group, "logical_volumes", &LogicalVolumes))
        return;
    Walk = LogicalVolumes;
    while (LvmNextSection(&Walk, Name, sizeof(Name), &Logical)) {
        if (LvmLvCount == LVM_LVS_MAX) break;
        if (!LvmHasFlag(Logical, "status", "VISIBLE") ||
            !LvmFindField(Logical, "id", Identifier, sizeof(Identifier)))
            continue;
        Lv = AllocateZeroPool(sizeof(LVM_LV));
        if (!Lv) return;
        if (LvmNormalizeId(Identifier, Lv->Id) &&
            LvmParseSegments(Logical, ExtentSize, Aliases, AliasCount, Lv) &&
            LvmPublish(Lv)) {
            continue;
        }
        LOG(2, LOG_LINE_NORMAL, L"LVM2: unsupported or incomplete logical volume skipped");
        FreePool(Lv);
    }
}
/* Read every physical-volume label without firmware mutations. Only metadata
 * whose checksum, UUID, sector bounds and linear extents all validate can
 * create virtual devices. Multiple metadata copies select highest VG seqno.
 */
BOOLEAN LvmDiscoverVolumes(VOID) {
    EFI_HANDLE *Handles = NULL;
    EFI_BLOCK_IO *Block;
    EFI_DISK_IO *Disk;
    EFI_STATUS Status;
    UINTN Count = 0, i, j;
    LVM_SPAN Whole, Group;
    CHAR8 GroupName[128], Id[128], NormalId[33];
    UINT64 Seq;
    BOOLEAN Publish = FALSE, Use;
    if (LvmEnumerated) return LvmLvCount > 0;
    LvmEnumerated = TRUE;
    Status = boot_call5_wrapper(BS->LocateHandleBuffer, ByProtocol,
                                &gMyEfiBlockIoProtocolGuid, NULL,
                                &Count, &Handles);
    if (EFI_ERROR(Status) || !Handles) return FALSE;
    for (i = 0; i < Count; ++i) {
        if (EFI_ERROR(boot_call3_wrapper(BS->HandleProtocol, Handles[i],
                     &gMyEfiBlockIoProtocolGuid, (VOID **)&Block)) ||
            EFI_ERROR(boot_call3_wrapper(BS->HandleProtocol, Handles[i],
                     &gMyEfiDiskIoProtocolGuid, (VOID **)&Disk)))
            continue;
        LvmDiscoverPhysical(Block, Disk);
    }
    FreePool(Handles);
    for (i = 0; i < LvmPvCount; ++i) {
        if (!LvmPvs[i].Metadata) continue;
        Whole.Current = LvmPvs[i].Metadata;
        Whole.End = Whole.Current;
        while (*Whole.End && Whole.End - Whole.Current < LVM_TEXT_MAX)
            Whole.End++;
        if (!LvmNextSection(&Whole, GroupName, sizeof(GroupName), &Group) ||
            !LvmFindField(Group, "id", Id, sizeof(Id)) ||
            !LvmNormalizeId(Id, NormalId) ||
            !LvmGetNumber(Group, "seqno", &Seq))
            continue;
        LvmCopy(LvmPvs[i].VgId, sizeof(LvmPvs[i].VgId), NormalId);
        LvmPvs[i].Sequence = Seq;
    }
    for (i = 0; i < LvmPvCount; ++i) {
        if (!LvmPvs[i].VgId[0]) continue;
        Use = TRUE;
        for (j = 0; j < LvmPvCount; ++j)
            if (j != i && LvmSame(LvmPvs[i].VgId, LvmPvs[j].VgId) &&
                (LvmPvs[j].Sequence > LvmPvs[i].Sequence ||
                 (LvmPvs[j].Sequence == LvmPvs[i].Sequence && j < i)))
                Use = FALSE;
        if (!Use) continue;
        Whole.Current = LvmPvs[i].Metadata;
        Whole.End = Whole.Current;
        while (*Whole.End && Whole.End - Whole.Current < LVM_TEXT_MAX)
            Whole.End++;
        if (LvmNextSection(&Whole, GroupName, sizeof(GroupName), &Group))
            LvmMapGroup(Group);
    }
    for (i = 0; i < LvmPvCount; ++i) {
        FreePool(LvmPvs[i].Metadata);
        LvmPvs[i].Metadata = NULL;
    }
    Publish = (LvmLvCount > 0);
    if (!Publish)
        LOG(2, LOG_LINE_NORMAL, L"LVM2: no supported linear volumes published");
    return Publish;
}
