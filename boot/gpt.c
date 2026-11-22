/*
 * Read-only GPT metadata acquisition for identifying partitions.
 * The protective MBR, primary GPT header and entry array originate from
 * untrusted blocks. Header CRC and entries CRC must both be valid before
 * a partition table is treated as coherent; this code does not repair disks.
 * Cached table records and copied partition outputs have different lifetimes.
 * Invalidating the global GPT cache must not leave callers retaining borrowed
 * pointers into it. A checksum is structural consistency, not authentication.
 * Cached table records and copied partition outputs have different lifetimes.
 * Invalidating the global GPT cache must not leave callers retaining borrowed
 * pointers into it. A checksum is structural consistency, not authentication.
 */

#pragma pack(0)
#include "gpt.h"
#include "lib.h"
#include "screen.h"
#include "crc32.h"
#include "../include/refit_call_wrapper.h"

#ifdef __MAKEWITH_TIANO
#define BlockIoProtocol gEfiBlockIoProtocolGuid
#endif

GPT_DATA *gPartitions = NULL;

/*
 * Allocates zero-initialised GPT bookkeeping. Ownership transfers to the
 * caller until ClearGptData releases the table and its subordinate buffers.
 */
GPT_DATA * AllocateGptData(VOID) {
    GPT_DATA *GptData;

    GptData = AllocateZeroPool(sizeof(GPT_DATA));
    if (GptData != NULL) {
        GptData->ProtectiveMBR = AllocateZeroPool(sizeof(MBR_RECORD));
        GptData->Header = AllocateZeroPool(sizeof(GPT_HEADER));
        if ((GptData->ProtectiveMBR == NULL) || (GptData->Header == NULL)) {
            MyFreePool(GptData->ProtectiveMBR);
            MyFreePool(GptData->Header);
            MyFreePool(GptData);
            GptData = NULL;
        }  
    }  
    return GptData;
}  

/*
 * Free a complete cached GPT table and its individually allocated
 * protective MBR, header and entries. A caller must also clear its
 * own pointer: this destructor does not alter the caller's variable.
 */
VOID ClearGptData(GPT_DATA *Data) {
    if (Data) {
        if (Data->ProtectiveMBR)
            MyFreePool(Data->ProtectiveMBR);
        if (Data->Header)
            MyFreePool(Data->Header);
        if (Data->Entries)
            MyFreePool(Data->Entries);
        MyFreePool(Data);
    }  
}  

/**
 * Require the protective MBR signature and at least one type-0xEE entry, the GPT
 * signature, revision 1.0 and 128-byte entries. Compute the header CRC with the
 * crc32 field temporarily zero, then restore it. The covered byte count is the
 * smaller of the recorded header size and the compiled header buffer.
 */
static BOOLEAN GptHeaderValid(GPT_DATA *GptData) {
    BOOLEAN IsValid;
    UINT32 CrcValue, StoredCrcValue;
    UINTN HeaderSize = sizeof(GPT_HEADER);

    if ((GptData == NULL) || (GptData->ProtectiveMBR == NULL) || (GptData->Header == NULL))
        return FALSE;

    IsValid = (GptData->ProtectiveMBR->MBRSignature == 0xAA55);
    IsValid = IsValid && ((GptData->ProtectiveMBR->partitions[0].type == 0xEE) ||
                          (GptData->ProtectiveMBR->partitions[1].type == 0xEE) ||
                          (GptData->ProtectiveMBR->partitions[2].type == 0xEE) ||
                          (GptData->ProtectiveMBR->partitions[3].type == 0xEE));

    IsValid = IsValid && ((GptData->Header->signature == 0x5452415020494645ULL) &&
                          (GptData->Header->spec_revision == 0x00010000) &&
                          (GptData->Header->entry_size == 128));

    if (IsValid) {
        if (GptData->Header->header_size < HeaderSize)
            HeaderSize = GptData->Header->header_size;
        StoredCrcValue = GptData->Header->header_crc32;
        GptData->Header->header_crc32 = 0;
        CrcValue = crc32(0x0, GptData->Header, HeaderSize);
        if (CrcValue != StoredCrcValue)
            IsValid = FALSE;
        GptData->Header->header_crc32 = StoredCrcValue;
    }  

    return IsValid;
}  

/**
 * Read the protective MBR from LBA 0, the primary header from LBA 1 and entries
 * from entry_lba. Validate the table checksum on the original bytes before
 * terminating the name field in each in-memory entry. ReadBlocks sizes are the
 * fixed structure sizes and must suit the device's block granularity.
 *
 * On success replace the previous *Data. On failure free the provisional table
 * and retain the caller's original pointer. BufferSize is 64-bit, but the
 * entry_count * 128 expression is evaluated at the width of entry_count before
 * assignment.
 */
EFI_STATUS ReadGptData(REFIT_VOLUME *Volume, GPT_DATA **Data) {
    EFI_STATUS Status = EFI_SUCCESS;
    UINT64     BufferSize;
    UINTN      i;
    GPT_DATA   *GptData = NULL;  

    if ((Volume == NULL) || (Data == NULL))
        return EFI_INVALID_PARAMETER;

    if ((Status == EFI_SUCCESS) && (Volume->BlockIO == NULL)) {
        Status = refit_call3_wrapper(BS->HandleProtocol, Volume->DeviceHandle,
                                     &BlockIoProtocol, (VOID **) &(Volume->BlockIO));
        if (EFI_ERROR(Status)) {
            Volume->BlockIO = NULL;
            Print(L"Warning: Can't get BlockIO protocol in ReadGptData().\n");
            Status = EFI_NOT_READY;
        }
    }  

    if ((Status == EFI_SUCCESS) && ((!Volume->BlockIO->Media->MediaPresent) ||
        (Volume->BlockIO->Media->LogicalPartition)))
            Status = EFI_NO_MEDIA;

    if (Status == EFI_SUCCESS) {
        GptData = AllocateGptData();  
        if (GptData == NULL) {
            Status = EFI_OUT_OF_RESOURCES;
        }  
    }  

    if (Status == EFI_SUCCESS) {
        Status = refit_call5_wrapper(Volume->BlockIO->ReadBlocks, Volume->BlockIO,
                                     Volume->BlockIO->Media->MediaId, 0,
                                     sizeof(MBR_RECORD), (VOID*) GptData->ProtectiveMBR);
    }

    if (Status == EFI_SUCCESS) {
        Status = refit_call5_wrapper(Volume->BlockIO->ReadBlocks, Volume->BlockIO,
                                     Volume->BlockIO->Media->MediaId, 1,
                                     sizeof(GPT_HEADER), GptData->Header);
    }

    if (Status == EFI_SUCCESS) {
        if (GptHeaderValid(GptData)) {
            /*
             * entry_count is a UINT32. With the literal 128 the product is
             * evaluated at that width before assignment to the wider
             * BufferSize. The allocation, read and CRC all use the product,
             * while the name-termination loop uses the original count.
             */
            BufferSize = GptData->Header->entry_count * 128;
            GptData->Entries = AllocatePool(BufferSize);
            if (GptData->Entries == NULL)
                Status = EFI_OUT_OF_RESOURCES;

            if (Status == EFI_SUCCESS)
                Status = refit_call5_wrapper(Volume->BlockIO->ReadBlocks, Volume->BlockIO,
                                             Volume->BlockIO->Media->MediaId,
                                             GptData->Header->entry_lba, BufferSize,
                                             GptData->Entries);

            if ((Status == EFI_SUCCESS) &&
                (crc32(0x0, GptData->Entries, BufferSize) != GptData->Header->entry_crc32))
                    Status = EFI_CRC_ERROR;

            if (Status == EFI_SUCCESS) {
                for (i = 0; i < GptData->Header->entry_count; i++)
                    GptData->Entries[i].name[35] = '\0';
            }  
        } else {
            Status = EFI_UNSUPPORTED;
        }  
    }  

    if (Status == EFI_SUCCESS) {

        ClearGptData(*Data);
        *Data = GptData;
    } else {
        ClearGptData(GptData);
    }  

    return Status;
}  

/**
 * Search each cached table by the unique partition GUID, in discovery order, and
 * return a separately allocated entry for the first match. A caller may retain
 * the copy after ForgetPartitionTables. Lookup cost is linear in the total number
 * of entries examined.
 */
GPT_ENTRY * FindPartWithGuid(EFI_GUID *Guid) {
    UINTN     i;
    GPT_ENTRY *Found = NULL;
    GPT_DATA  *GptData;

    if ((Guid == NULL) || (gPartitions == NULL))
        return NULL;

    GptData = gPartitions;
    while ((GptData != NULL) && (!Found)) {
        i = 0;
        while ((i < GptData->Header->entry_count) && (!Found)) {
            if (GuidsAreEqual((EFI_GUID*) &(GptData->Entries[i].partition_guid), Guid)) {
                Found = AllocateZeroPool(sizeof(GPT_ENTRY));
                MyCopyMem(Found, &GptData->Entries[i], sizeof(GPT_ENTRY));
            } else {
                i++;
            }  
        }  
        GptData = GptData->NextEntry;
    }  
    return Found;
}  

/*
 * Drop all globally cached GPT tables before rebuilding discovery.
 * Partitions returned by FindPartWithGuid are independent copies, but
 * borrowed pointers into the cache become invalid after this traversal.
 */
VOID ForgetPartitionTables(VOID) {
    GPT_DATA  *Next;

    while (gPartitions != NULL) {
        Next = gPartitions->NextEntry;
        ClearGptData(gPartitions);
        gPartitions = Next;
    }  
}  

/*
 * Read a candidate volume's GPT and append a successful result in
 * discovery order. The global list is not sorted by partition GUID;
 * a read failure does not invalidate previously cached tables.
 */
VOID AddPartitionTable(REFIT_VOLUME *Volume) {
    GPT_DATA    *GptData = NULL, *GptList;
    EFI_STATUS  Status;
    UINTN       NumTables = 1;

    Status = ReadGptData(Volume, &GptData);
    if (Status == EFI_SUCCESS) {
        if (gPartitions == NULL) {
            gPartitions = GptData;
        } else {
            GptList = gPartitions;
            while (GptList->NextEntry != NULL) {
                GptList = GptList->NextEntry;
                NumTables++;
            }  
            GptList->NextEntry = GptData;
            NumTables++;
        }  
    } else if (GptData != NULL) {
        ClearGptData(GptData);
        NumTables = 0;
    }  
}  
