/*
 * Firmware image loading boundary for EFI applications, EFI drivers and
 * EFI-stub kernels. A displayed file can still fail header validation,
 * firmware LoadImage, image authentication or StartImage.
 * Compressed images require explicit allocation and decompression limits.
 */

#include "global.h"
#include "screen.h"
#include "lib.h"
#include "mok.h"
#include "mystrings.h"
#include "driver_support.h"
#include "../include/refit_call_wrapper.h"
#include "launch_efi.h"
#include "log.h"
#include "scan.h"

#ifdef __MAKEWITH_GNUEFI
#ifndef EFI_SECURITY_VIOLATION
#define EFI_SECURITY_VIOLATION    EFIERR (26)
#endif
#endif

#if defined (EFIX64)
#define EFI_STUB_ARCH           0x0000866400004550
#elif defined (EFI32)
#define EFI_STUB_ARCH           0x0000014c00004550
#elif defined (EFIAARCH64)
#define EFI_STUB_ARCH           0x0000aa6400004550
#else
#endif
UINT64 PESig = EFI_STUB_ARCH;

#define FAT_ARCH                0x0ef1fab9  

#define EFI_HEADER_SIZE 4096

/*
 * Explains a firmware security rejection without weakening the platform's
 * trust requirements or silently enrolling an executable.
 */
static VOID WarnSecureBootError(CHAR16 *Name, BOOLEAN Verbose) {
    if (Name == NULL)
        Name = L"the loader";

    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_ERROR);
    Print(L"Secure Boot validation failure loading %s!\n", Name);
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
    if (Verbose && secure_mode()) {
        Print(L"\nThis computer is configured with Secure Boot active, but\n%s has failed validation.\n", Name);
        Print(L"\nYou can:\n * Launch another boot loader\n");
        Print(L" * Disable Secure Boot in your firmware\n");
        Print(L" * Sign %s with a machine owner key (MOK)\n", Name);
        Print(L" * Use a MOK utility (often present on the second row) to add a MOK with which\n");
        Print(L"   %s has already been signed.\n", Name);
        Print(L" * Use a MOK utility to register %s (\"enroll its hash\") without\n", Name);
        Print(L"   signing it.\n");
        Print(L"\nSee https://github.com/Infiltrator-Projects/Bootsecureboot.html for more information\n");
        PauseForKey();
    }  
}  

/**
 * Classify candidates during scanning. The native check requires a complete
 * 4096-byte probe and recognises either the MZ/PE signature and target machine or
 * the fat-image magic. With gzip enabled a matching two-byte gzip signature
 * selects the decoder path. LoadImage performs the later firmware load decision.
 */
UINTN IsValidLoader(EFI_FILE_PROTOCOL *RootDir, CHAR16 *FileName) {
    UINTN           LoaderType = LOADER_TYPE_EFI;
#if defined (EFIX64) | defined (EFI32) | defined (EFIAARCH64)
    BOOLEAN         IsValid;
    EFI_STATUS      Status;
    EFI_FILE_HANDLE FileHandle;
    CHAR8           *Header;
    CHAR16          *TypeDesc = L"a valid";
    UINTN           LoadedSize = EFI_HEADER_SIZE;
    UINTN           SignaturePos;

    Header = AllocatePool(EFI_HEADER_SIZE);
    if (!Header) {
        LOG(1, LOG_LINE_NORMAL, L"Unable to allocate memory in IsValidLoader()!");
        return LOADER_TYPE_INVALID;
    }
    if ((RootDir == NULL) || (FileName == NULL)) {

        return LOADER_TYPE_EFI;
    }  

    Status = refit_call5_wrapper(RootDir->Open, RootDir, &FileHandle, FileName, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status))
        return LOADER_TYPE_INVALID;

    Status = refit_call3_wrapper(FileHandle->Read, FileHandle, &LoadedSize, Header);
    refit_call1_wrapper(FileHandle->Close, FileHandle);

    if (EFI_ERROR(Status)) {
        MyFreePool(Header);
        LOG(1, LOG_LINE_NORMAL, L"Error reading %s to determine if it's a valid loader!", FileName);
        return LOADER_TYPE_INVALID;
    }
    SignaturePos = *(UINT32 *)&Header[0x3c];
    LOG(4, LOG_LINE_NORMAL, L"In IsValidLoader(), LoadedSize is %llu, SignaturePos is %llu",
        (UINT64) LoadedSize, (UINT64) SignaturePos);

    IsValid = (LoadedSize == EFI_HEADER_SIZE) &&
              ((Header[0] == 'M' && Header[1] == 'Z' &&
               (SignaturePos < (EFI_HEADER_SIZE - 8)) &&
               (CompareMem(&Header[SignaturePos], &PESig, 6) == 0)) ||
              (*(UINT32 *)Header == FAT_ARCH));
    if (!IsValid) {

        if ((Header[0] == (CHAR8) 0x1F && Header[1] == (CHAR8) 0x8B) && GlobalConfig.GzippedLoaders) {
            LoaderType = LOADER_TYPE_GZIP;
            TypeDesc = L"a gzipped";
        } else {
            LoaderType = LOADER_TYPE_INVALID;
            TypeDesc = L"an invalid";
        }
    }
    LOG(1, LOG_LINE_NORMAL, L"'%s' is %s loader file", FileName, TypeDesc);
    MyFreePool(Header);
#endif
    return LoaderType;
}  

/**
 * Native images reach LoadImage by device path. Gzip inputs are read into memory
 * and decoded into a bounded pool allocation. Firmware receives the exact number
 * of output bytes from a successful gunzip call. The current allocation policy
 * allows ten times the compressed size, up to 512 MiB.
 *
 * Once firmware supplies a child handle, its LoadedImage protocol borrows the
 * option string until StartImage returns. BOOT closes its own open volume handles
 * before transfer, then reacquires them on return. Returning applications are
 * unloaded, while drivers remain resident for their protocol bindings.
 *
 * Security and failure boundaries: IsValidLoader is a type/format gate, not
 * cryptographic authentication; firmware LoadImage and the active Secure Boot
 * policy determine whether the image can be accepted. Decompression refuses
 * a zero-length input or one exceeding 512 MiB / 10 before multiplication.
 * The output buffer capacity is ten times compressed length, not the length
 * asserted by gzip metadata, and decoded length is supplied to LoadImage only
 * after a successful decoder result. A return from StartImage is not evidence
 * that the operating system started successfully.
 *
 * Callers supply borrowed Volume/Filename/LoadOptions pointers. The function
 * duplicates LoadOptions for the child LoadedImage protocol and must retain
 * that allocation until StartImage or unwind has finished. Firmware handles
 * and protocol pointers are not caller-owned pool allocations.
 */
EFI_STATUS StartEFIImage(IN REFIT_VOLUME *Volume,
                         IN CHAR16 *Filename,
                         IN CHAR16 *LoadOptions,
                         IN CHAR16 *ImageTitle,
                         IN CHAR8 OSType,
                         IN BOOLEAN Verbose,
                         IN BOOLEAN IsDriver)
{
    EFI_STATUS              Status, ReturnStatus;
    EFI_HANDLE              ChildImageHandle, ChildImageHandle2;
    EFI_DEVICE_PATH         *DevicePath;
    EFI_LOADED_IMAGE        *ChildLoadedImage = NULL;
    CHAR16                  *ErrorInfo;
    CHAR16                  *FullLoadOptions = NULL;
    CHAR16                  *EspGUID;
    UINT8                   *gzData = NULL, *ImageData = NULL;
    UINTN                   gzSize, ImageCapacity, DecodedSize;
    UINTN                   LoaderType;
    int                     gzStatus;
    EFI_GUID                SystemdGuid = SYSTEMD_GUID_VALUE;

    if (!Volume || !Volume->RootDir || !Filename)
        return EFI_INVALID_PARAMETER;

    if (LoadOptions != NULL) {
        FullLoadOptions = StrDuplicate(LoadOptions);
        if (OSType == 'M') {
            MergeStrings(&FullLoadOptions, L" ", 0);

        }  
    }  
    LOG(1, LOG_LINE_NORMAL, L"Starting %s", ImageTitle);
    LOG(1, LOG_LINE_NORMAL, L"Using load options '%s'", FullLoadOptions ? FullLoadOptions : L"");
    if (IsDriver)
        LOG(1, LOG_LINE_NORMAL, L"Note: %s is a driver", ImageTitle);
    if (Verbose)
        Print(L"Starting %s\nUsing load options '%s'\n", ImageTitle, FullLoadOptions ? FullLoadOptions : L"");

    ReturnStatus = Status = EFI_NOT_FOUND;   

    LoaderType = IsValidLoader(Volume->RootDir, Filename);
    if ((LoaderType == LOADER_TYPE_EFI) || (LoaderType == LOADER_TYPE_GZIP)) {
        DevicePath = FileDevicePath(Volume->DeviceHandle, Filename);

        if (LoaderType == LOADER_TYPE_EFI) {

            ReturnStatus = Status = refit_call6_wrapper(BS->LoadImage, FALSE, SelfImageHandle, DevicePath,
                                                        NULL, 0, &ChildImageHandle);
        } else {
            Status = egLoadFile(Volume->RootDir, Filename, &gzData, &gzSize);
            if (!EFI_ERROR(Status)) {

                if (!gzSize || gzSize > (UINTN)(512U * 1024U * 1024U / 10U)) {
                    LOG(1, LOG_LINE_NORMAL, L"Gzip EFI image is too large");
                    ReturnStatus = Status = EFI_OUT_OF_RESOURCES;
                } else {
                    ImageCapacity = gzSize * 10;
                    ImageData = AllocateZeroPool(ImageCapacity);
                    if (!ImageData) {
                        ReturnStatus = Status = EFI_OUT_OF_RESOURCES;
                    } else {
                        gzStatus = gunzip(gzData, gzSize, NULL, NULL,
                                         ImageData, ImageCapacity, &DecodedSize);
                        LOG(1, LOG_LINE_NORMAL, L"gunzip status %d, decoded %lu bytes",
                            gzStatus, (UINT64)DecodedSize);
                        if (gzStatus == 0 && DecodedSize > 0 &&
                            DecodedSize <= ImageCapacity) {
                            ReturnStatus = Status = refit_call6_wrapper(
                                BS->LoadImage, FALSE, SelfImageHandle,
                                DevicePath, ImageData, DecodedSize,
                                &ChildImageHandle);
                        } else {
                            LOG(1, LOG_LINE_NORMAL, L"Invalid gzip EFI image size or content");
                            ReturnStatus = Status = EFI_LOAD_ERROR;
                        }
                    }
                }
            }
        }
        if (secure_mode() && ShimLoaded() && !EFI_ERROR(Status)) {

            LOG(3, LOG_LINE_NORMAL, L"Employing Shim LoadImage() hack");
            refit_call6_wrapper(BS->LoadImage, FALSE, SelfImageHandle, GlobalConfig.SelfDevicePath,
                                NULL, 0, &ChildImageHandle2);
        }
    } else {
        Print(L"Invalid loader file!\n");
        LOG(1, LOG_LINE_NORMAL, L"Invalid loader file!");
        ReturnStatus = EFI_LOAD_ERROR;
    }
    if ((Status == EFI_ACCESS_DENIED) || (Status == EFI_SECURITY_VIOLATION)) {
        LOG(1, LOG_LINE_NORMAL, L"Secure boot error while loading '%s'; Status = %d", ImageTitle, Status);
        WarnSecureBootError(ImageTitle, Verbose);
        goto bailout;
    }
    ErrorInfo = PoolPrint(L"while loading %s", ImageTitle);
    if (CheckError(Status, ErrorInfo)) {
        MyFreePool(ErrorInfo);
        goto bailout;
    }
    MyFreePool(ErrorInfo);

    ReturnStatus = Status = refit_call3_wrapper(BS->HandleProtocol, ChildImageHandle, &LoadedImageProtocol,
                                                (VOID **) &ChildLoadedImage);
    if (CheckError(Status, L"while getting a LoadedImageProtocol handle")) {
        goto bailout_unload;
    }
    /*
     * LoadedImage borrows the option buffer for StartImage. Its size is in
     * bytes and includes the UTF-16 terminator. BOOT keeps the buffer until
     * the call returns or the launch unwinds.
     */
    ChildLoadedImage->LoadOptions = (VOID *)FullLoadOptions;
    ChildLoadedImage->LoadOptionsSize = FullLoadOptions ? ((UINT32)StrLen(FullLoadOptions) + 1) * sizeof(CHAR16) : 0;

    if ((GlobalConfig.WriteSystemdVars) && ((OSType == 'L') || (OSType == 'E') || (OSType == 'G'))) {

        EspGUID = GuidAsString(&(SelfVolume->PartGuid));
        LOG(1, LOG_LINE_NORMAL, L"Setting systemd's LoaderDevicePartUUID variable to %s", EspGUID);
        Status = EfivarSetRaw(&SystemdGuid, L"LoaderDevicePartUUID", (CHAR8 *) EspGUID,
                              StrLen(EspGUID) * 2 + 2, FALSE);
        if (EFI_ERROR(Status)) {
            LOG(1, LOG_LINE_NORMAL,
                L"Error %d when trying to set LoaderDevicePartUUID EFI variable", Status);
        }
        MyFreePool(EspGUID);
    }  

    LOG(1, LOG_LINE_NORMAL, L"Launching '%s'", ImageTitle);
    /*
     * Release BOOT's open filesystem handles before handing control to the
     * child. A returning child may have changed controller bindings, so the
     * return path reacquires BOOT's handles. Started drivers are kept
     * resident for subsequent protocol calls.
     */
    UninitRefitLib();

    ReturnStatus = Status = refit_call3_wrapper(BS->StartImage, ChildImageHandle, NULL, NULL);

    ErrorInfo = PoolPrint(L"returned from %s", ImageTitle);
    CheckError(Status, ErrorInfo);
    MyFreePool(ErrorInfo);
    if (IsDriver) {

        ConnectFilesystemDriver(ChildImageHandle);
    }

    ReinitRefitLib();
    LOG(1, LOG_LINE_NORMAL, L"Program has returned %d", Status);

bailout_unload:

    if (!IsDriver)
        Status = refit_call1_wrapper(BS->UnloadImage, ChildImageHandle);

bailout:
    MyFreePool(gzData);
    MyFreePool(ImageData);
    MyFreePool(FullLoadOptions);
    if (!IsDriver)
        FinishExternalScreen();

    LOG(1, LOG_LINE_THIN_SEP, L"Next loader");
    return ReturnStatus;
}  

/*
 * Request entry to firmware setup through the 64-bit OsIndications variable
 * before a cold reset. The read path dereferences only the first CHAR8 of the
 * returned buffer. Existing indication bits in higher bytes are therefore not
 * carried into the new value.
 */
EFI_STATUS RebootIntoFirmware(VOID) {
    CHAR8 *b;
    UINTN size;
    UINT64 osind;
    EFI_STATUS err;

    osind = EFI_OS_INDICATIONS_BOOT_TO_FW_UI;

    err = EfivarGetRaw(&GlobalGuid, L"OsIndications", &b, &size);
    if (err == EFI_SUCCESS)
        osind |= (UINT64)*b;
    MyFreePool(b);

    err = EfivarSetRaw(&GlobalGuid, L"OsIndications", (CHAR8 *)&osind, sizeof(UINT64), TRUE);
    if (err != EFI_SUCCESS)
        return err;

    LOG(1, LOG_LINE_SEPARATOR, L"Rebooting into the computer's firmware");
    UninitRefitLib();
    refit_call4_wrapper(RT->ResetSystem, EfiResetCold, EFI_SUCCESS, 0, NULL);
    ReinitRefitLib();
    LOG(1, LOG_LINE_NORMAL, L"Error calling ResetSystem: %r", err);
    Print(L"Error calling ResetSystem: %r\n", err);
    PauseForKey();
    return err;
}  

/**
 * Write the selected UINT16 Boot#### number to BootNext, save the menu title and
 * request a cold reset. A failure writing BootNext returns before the reset. If
 * ResetSystem returns unexpectedly, reopen the BOOT library handles before
 * reporting the failure.
 */
VOID RebootIntoLoader(LOADER_ENTRY *Entry) {
    EFI_STATUS Status;

    LOG(1, LOG_LINE_SEPARATOR, L"Rebooting into EFI loader '%s' (Boot%04x)",
        Entry->Title, Entry->EfiBootNum);
    Status = EfivarSetRaw(&GlobalGuid, L"BootNext", (CHAR8*) &(Entry->EfiBootNum), sizeof(UINT16), TRUE);
    if (EFI_ERROR(Status)) {
        LOG(1, LOG_LINE_NORMAL, L"Error: %d", Status);
        Print(L"Error: %d\n", Status);
        return;
    }
    StoreLoaderName(Entry->me.Title);
    LOG(1, LOG_LINE_NORMAL, L"Attempting to reboot....", Entry->Title, Entry->EfiBootNum);
    UninitRefitLib();
    refit_call4_wrapper(RT->ResetSystem, EfiResetCold, EFI_SUCCESS, 0, NULL);
    ReinitRefitLib();
    LOG(1, LOG_LINE_NORMAL, L"Error calling ResetSystem: %r", Status);
    Print(L"Error calling ResetSystem: %r\n", Status);
    PauseForKey();
}  

/*
 * On x86, manipulates IA32_FEATURE_CONTROL (MSR 0x3a) before image dispatch.
 * Its lock bit is effectively irreversible until reset on supported CPUs;
 * this is a firmware/processor side effect, not merely a menu preference.
 * The caller gates this behaviour behind EnableAndLockVMX.
 */
static VOID DoEnableAndLockVMX(VOID) {
#if defined (EFIX64) | defined (EFI32)
    UINT32 msr = 0x3a;
    UINT32 low_bits = 0, high_bits = 0;

    LOG(1, LOG_LINE_NORMAL, L"Attempting to enable and lock VMX");

    __asm__ volatile ("rdmsr" : "=a" (low_bits), "=d" (high_bits) : "c" (msr));

    if ((low_bits & 1) == 0) {
        high_bits = 0;
        low_bits = 0x05;
        msr = 0x3a;
        __asm__ volatile ("wrmsr" : : "c" (msr), "a" (low_bits), "d" (high_bits));
    }
#endif
}  

/*
 * Transfers control to the selected EFI loader. Successful image dispatch
 * and successful operating-system startup are different observations.
 */
VOID StartLoader(LOADER_ENTRY *Entry, CHAR16 *SelectionName) {
    CHAR16 *LoaderPath;

    LOG(1, LOG_LINE_SEPARATOR, L"Launching '%s'", SelectionName);
    if (GlobalConfig.EnableAndLockVMX) {
        DoEnableAndLockVMX();
    }

    LoaderPath = Basename(Entry->LoaderPath);
    BeginExternalScreen(Entry->UseGraphicsMode, L"Booting OS");
    StoreLoaderName(SelectionName);
    StartEFIImage(Entry->Volume, Entry->LoaderPath, Entry->LoadOptions,
                  LoaderPath, Entry->OSType, !Entry->UseGraphicsMode, FALSE);
    MyFreePool(LoaderPath);
}  

/*
 * Uses the same EFI image-launch boundary as operating-system entries but
 * presents the selected item as an external utility. Its return to BOOT is
 * permissible and does not imply that firmware restarted the machine.
 */
VOID StartTool(IN LOADER_ENTRY *Entry) {
    CHAR16 *LoaderPath;

    LOG(1, LOG_LINE_SEPARATOR, L"Starting '%s'", Entry->me.Title);
    BeginExternalScreen(Entry->UseGraphicsMode, Entry->me.Title + 6);   
    StoreLoaderName(Entry->me.Title);
    LoaderPath = Basename(Entry->LoaderPath);
    StartEFIImage(Entry->Volume, Entry->LoaderPath, Entry->LoadOptions,
                  LoaderPath, Entry->OSType, TRUE, FALSE);
    MyFreePool(LoaderPath);
}  
