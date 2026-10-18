/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise the production icon reader and decoders with EFI directory handles. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <efi.h>
#include <efilib.h>
#include "../refind/global.h"
#include "../refind/lib.h"
#include "../libeg/libeg.h"

/* GNU-EFI's production call wrappers enter firmware using the Microsoft ABI. */
#define MOCK_EFIAPI __attribute__((ms_abi))

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "icon directory test failed at line %d: %s\n", __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

REFIT_CONFIG GlobalConfig;
EFI_FILE_PROTOCOL *SelfDir;
CHAR16 *gLogTemp;

static EFI_FILE_PROTOCOL BootDir, IconsDir, IconFile;
static EFI_BOOT_SERVICES Services;
static UINT8 *PngData;
static UINTN PngSize;
static UINTN DirAttempts, FileAttempts, DirCloses, FileCloses, Stalls;
static UINTN DirFailures, FileFailures, ShortReads;
static EFI_STATUS DirFailure;
static BOOLEAN MissingFile;

static EFI_STATUS MOCK_EFIAPI Allocate(EFI_MEMORY_TYPE Type, UINTN Size, VOID **Buffer) {
    (void)Type;
    *Buffer = malloc(Size ? Size : 1);
    return *Buffer ? EFI_SUCCESS : EFI_OUT_OF_RESOURCES;
}

static EFI_STATUS MOCK_EFIAPI Release(VOID *Buffer) {
    free(Buffer);
    return EFI_SUCCESS;
}

static VOID MOCK_EFIAPI Copy(VOID *Destination, VOID *Source, UINTN Size) {
    memcpy(Destination, Source, Size);
}

static EFI_STATUS MOCK_EFIAPI Stall(UINTN Microseconds) {
    CHECK(Microseconds == 100000);
    Stalls++;
    return EFI_SUCCESS;
}

VOID MyFreePool(VOID *Pointer) {
    if (Pointer)
        FreePool(Pointer);
}

VOID WriteToLog(CHAR16 **Message, UINTN Type) {
    (void)Type;
    MyFreePool(*Message);
    *Message = NULL;
}

static EFI_STATUS MOCK_EFIAPI Open(EFI_FILE_PROTOCOL *Parent, EFI_FILE_PROTOCOL **Child,
                             CHAR16 *Name, UINT64 Mode, UINT64 Attributes) {
    CHECK(Mode == EFI_FILE_MODE_READ);
    CHECK(Attributes == 0);
    *Child = NULL;
    if (Parent == &BootDir &&
        (StrCmp(Name, L"icons") == 0 || StrCmp(Name, L"theme\\icons") == 0)) {
        DirAttempts++;
        if (DirFailures) {
            DirFailures--;
            return DirFailure;
        }
        *Child = &IconsDir;
        return EFI_SUCCESS;
    }
    if (Parent == &BootDir && StrCmp(Name, L"missing") == 0) {
        DirAttempts++;
        return EFI_NOT_FOUND;
    }
    /* A directory handle accepts leaf names. Reject combined paths to check
     * that the reader opens a directory handle before reading its icons. */
    CHECK(Parent == &BootDir || Parent == &IconsDir);
    FileAttempts++;
    if (StrCmp(Name, L"os_linux.png") != 0 || MissingFile)
        return EFI_NOT_FOUND;
    if (FileFailures) {
        FileFailures--;
        return EFI_NOT_READY;
    }
    *Child = &IconFile;
    return EFI_SUCCESS;
}

static EFI_STATUS MOCK_EFIAPI Close(EFI_FILE_PROTOCOL *File) {
    CHECK(File != &BootDir); /* The caller owns the base handle. */
    if (File == &IconsDir)
        DirCloses++;
    else {
        CHECK(File == &IconFile);
        FileCloses++;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS MOCK_EFIAPI Info(EFI_FILE_PROTOCOL *File, EFI_GUID *Type,
                             UINTN *Size, VOID *Buffer) {
    UINTN Required = sizeof(EFI_FILE_INFO) + 16;
    EFI_FILE_INFO *Info = Buffer;
    (void)Type;
    if (*Size < Required) {
        *Size = Required;
        return EFI_BUFFER_TOO_SMALL;
    }
    memset(Buffer, 0, Required);
    Info->Size = Required;
    Info->FileSize = File == &IconFile ? PngSize : 0;
    Info->Attribute = File == &IconFile ? 0 : EFI_FILE_DIRECTORY;
    *Size = Required;
    return EFI_SUCCESS;
}

static EFI_STATUS MOCK_EFIAPI Read(EFI_FILE_PROTOCOL *File, UINTN *Size, VOID *Buffer) {
    CHECK(File == &IconFile);
    CHECK(*Size == PngSize);
    if (ShortReads) {
        ShortReads--;
        (*Size)--;
    }
    memcpy(Buffer, PngData, *Size);
    return EFI_SUCCESS;
}

static VOID Reset(VOID) {
    DirAttempts = FileAttempts = DirCloses = FileCloses = Stalls = 0;
    DirFailures = FileFailures = ShortReads = 0;
    DirFailure = EFI_DEVICE_ERROR;
    MissingFile = FALSE;
    GlobalConfig.IconsDir = NULL;
}

static VOID ExpectImage(EG_IMAGE *Image) {
    CHECK(Image != NULL);
    CHECK(Image->Width == 128 && Image->Height == 128);
    CHECK(Image->PixelData != NULL);
    egFreeImage(Image);
}

int main(int argc, char **argv) {
    FILE *Input;
    CHECK(argc == 2);
    Input = fopen(argv[1], "rb");
    CHECK(Input != NULL);
    CHECK(fseek(Input, 0, SEEK_END) == 0);
    PngSize = (UINTN)ftell(Input);
    CHECK(PngSize > 32);
    rewind(Input);
    PngData = malloc(PngSize);
    CHECK(PngData != NULL && fread(PngData, 1, PngSize, Input) == PngSize);
    fclose(Input);

    Services.AllocatePool = (EFI_ALLOCATE_POOL)Allocate;
    Services.FreePool = (EFI_FREE_POOL)Release;
    Services.CopyMem = (EFI_COPY_MEM)Copy;
    Services.Stall = (EFI_STALL)Stall;
    BS = &Services;
    BootDir.Open = IconsDir.Open = (EFI_FILE_OPEN)Open;
    BootDir.Close = IconsDir.Close = IconFile.Close = (EFI_FILE_CLOSE)Close;
    BootDir.GetInfo = IconsDir.GetInfo = IconFile.GetInfo = (EFI_FILE_GET_INFO)Info;
    IconFile.Read = (EFI_FILE_READ)Read;
    SelfDir = &BootDir;

    Reset();
    ExpectImage(egLoadIconAnyType(&BootDir, L"icons", L"os_linux", 128));
    CHECK(DirAttempts == 1 && DirCloses == 1 && FileCloses == 1 && Stalls == 0);

    Reset();
    ExpectImage(egLoadIconAnyType(&BootDir, L"theme\\icons", L"os_linux", 128));
    CHECK(DirAttempts == 1 && DirCloses == 1);

    Reset();
    ExpectImage(egLoadIconAnyType(&BootDir, L"", L"os_linux", 128));
    CHECK(DirAttempts == 0 && DirCloses == 0 && FileCloses == 1);

    Reset();
    ExpectImage(egLoadIconAnyType(&BootDir, L".", L"os_linux", 128));
    CHECK(DirAttempts == 0 && DirCloses == 0);

    Reset();
    ExpectImage(egLoadIconAnyType(&BootDir, NULL, L"os_linux", 128));
    CHECK(DirAttempts == 0 && DirCloses == 0);

    Reset();
    DirFailures = 1;
    ExpectImage(egLoadIconAnyType(&BootDir, L"icons", L"os_linux", 128));
    CHECK(DirAttempts == 2 && DirCloses == 1 && Stalls == 1);

    Reset();
    DirFailures = 3;
    CHECK(egLoadIconAnyType(&BootDir, L"icons", L"os_linux", 128) == NULL);
    CHECK(DirAttempts == 3 && DirCloses == 0 && FileAttempts == 0 && Stalls == 2);

    Reset();
    DirFailures = 1;
    DirFailure = EFI_NOT_FOUND;
    CHECK(egLoadIconAnyType(&BootDir, L"icons", L"os_linux", 128) == NULL);
    CHECK(DirAttempts == 1 && FileAttempts == 0 && Stalls == 0);

    Reset();
    MissingFile = TRUE;
    CHECK(egLoadIconAnyType(&BootDir, L"icons", L"os_linux", 128) == NULL);
    CHECK(DirCloses == 1 && FileAttempts == 5 && FileCloses == 0 && Stalls == 0);

    Reset();
    FileFailures = 1;
    ExpectImage(egLoadIconAnyType(&BootDir, L"icons", L"os_linux", 128));
    CHECK(DirAttempts == 1 && FileAttempts == 2 && DirCloses == 1 && Stalls == 1);

    Reset();
    ShortReads = 1;
    ExpectImage(egLoadIconAnyType(&BootDir, L"icons", L"os_linux", 128));
    CHECK(DirCloses == 1 && FileCloses == 2 && Stalls == 1);

    Reset();
    GlobalConfig.IconsDir = L"missing";
    ExpectImage(egFindIcon(L"os_linux", 128));
    CHECK(DirAttempts == 2 && DirCloses == 1 && FileCloses == 1 && Stalls == 0);

    CHECK(egLoadIconAnyType(NULL, L"icons", L"os_linux", 128) == NULL);
    CHECK(egLoadIconAnyType(&BootDir, L"icons", NULL, 128) == NULL);
    free(PngData);
    puts("Icon directory tests passed: real PNG decode, relative/custom directories, legacy direct paths, bounded retries, short reads and handle ownership.");
    return EXIT_SUCCESS;
}
