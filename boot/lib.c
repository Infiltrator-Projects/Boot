/*
 * Firmware-wide services: volumes, device paths, files and EFI variables.
 * Protocol handles, allocation ownership, disk identity and error statuses
 * cross security and lifetime boundaries here. A recognised volume is not
 * necessarily readable, and an EFI file is not automatically executable.
 */

#pragma pack(0)
#include "global.h"
#include "lib.h"
#include "icns.h"
#include "screen.h"
#include "../include/refit_call_wrapper.h"
#include "../include/RemovableMedia.h"
#include "gpt.h"
#include "config.h"
#include "driver_support.h"
#include "log.h"
#include "mystrings.h"

#ifdef __MAKEWITH_GNUEFI
#define EfiReallocatePool ReallocatePool
#else
#define LibLocateHandle gBS->LocateHandleBuffer
#define DevicePathProtocol gEfiDevicePathProtocolGuid
#define BlockIoProtocol gEfiBlockIoProtocolGuid
#define LibFileSystemInfo EfiLibFileSystemInfo
#define LibOpenRoot EfiLibOpenRoot
EFI_DEVICE_PATH EndDevicePath[] = {
   {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {END_DEVICE_PATH_LENGTH, 0}}
};
#endif

#define FAT_MAGIC                        0xAA55
#define EXT2_SUPER_MAGIC                 0xEF53
#define HFSPLUS_MAGIC1                   0x2B48
#define HFSPLUS_MAGIC2                   0x5848
#define REISERFS_SUPER_MAGIC_STRING      "ReIsErFs"
#define REISER2FS_SUPER_MAGIC_STRING     "ReIsEr2Fs"
#define REISER2FS_JR_SUPER_MAGIC_STRING  "ReIsEr3Fs"
#define BTRFS_SIGNATURE                  "_BHRfS_M"
#define XFS_SIGNATURE                    "XFSB"
#define JFS_SIGNATURE                    "JFS1"
#define NTFS_SIGNATURE                   "NTFS    "
#define FAT12_SIGNATURE                  "FAT12   "
#define FAT16_SIGNATURE                  "FAT16   "
#define FAT32_SIGNATURE                  "FAT32   "

#if defined (EFIX64)
EFI_GUID gFreedesktopRootGuid = { 0x4f68bce3, 0xe8cd, 0x4db1, { 0x96, 0xe7, 0xfb, 0xca, 0xf9, 0x84, 0xb7, 0x09 }};
#elif defined (EFI32)
EFI_GUID gFreedesktopRootGuid = { 0x44479540, 0xf297, 0x41b2, { 0x9a, 0xf7, 0xd1, 0x31, 0xd5, 0xf0, 0x45, 0x8a }};
#elif defined (EFIAARCH64)
EFI_GUID gFreedesktopRootGuid = { 0xb921b045, 0x1df0, 0x41c3, { 0xaf, 0x44, 0x4c, 0x6f, 0x28, 0x0d, 0x3f, 0xae }};
#else

EFI_GUID gFreedesktopRootGuid = { 0x69dad710, 0x2ce4, 0x4e3c, { 0xb1, 0x6c, 0x21, 0xa1, 0xd4, 0x9a, 0xbe, 0xd3 }};
#endif

EFI_HANDLE         SelfImageHandle;
EFI_LOADED_IMAGE   *SelfLoadedImage;
EFI_FILE_PROTOCOL  *SelfRootDir;
EFI_FILE_PROTOCOL  *SelfDir;
CHAR16             *SelfDirPath;

REFIT_VOLUME       *SelfVolume = NULL;
REFIT_VOLUME       **Volumes = NULL;
UINTN              VolumesCount = 0;
extern EFI_GUID    BootGuid;

EFI_FILE_PROTOCOL  *gVarsDir = NULL;

#define SECTOR_SIZE 4096

#define SAMPLE_SIZE 69632  

VOID CleanUpPathNameSlashes(IN OUT CHAR16 *PathName) {
    UINTN Source = 0, Dest = 0;

    if ((PathName == NULL) || (PathName[0] == '\0'))
        return;

    while (PathName[Source] != '\0') {
        if ((PathName[Source] == L'/') || (PathName[Source] == L'\\')) {
            if (Dest == 0) {  
                Source++;
            } else {
                PathName[Dest] = L'\\';
                do {  
                    Source++;
                } while ((PathName[Source] == L'/') || (PathName[Source] == L'\\'));
                Dest++;
            }  
        } else {  
            PathName[Dest] = PathName[Source];
            Source++;
            Dest++;
        }  
    }  
    if ((Dest > 0) && (PathName[Dest - 1] == L'\\'))
        Dest--;
    PathName[Dest] = L'\0';
    if (PathName[0] == L'\0') {
        PathName[0] = L'\\';
        PathName[1] = L'\0';
    }
}  

CHAR16* SplitDeviceString(IN OUT CHAR16 *InString) {
    INTN i;
    CHAR16 *FileName = NULL;
    BOOLEAN Found = FALSE;

    if (InString != NULL) {
        i = StrLen(InString) - 1;
        while ((i >= 0) && (!Found)) {
            if (InString[i] == L')') {
                Found = TRUE;
                FileName = StrDuplicate(&InString[i + 1]);
                CleanUpPathNameSlashes(FileName);
                InString[i + 1] = '\0';
            }  
            i--;
        }  
        if (FileName == NULL)
            FileName = StrDuplicate(InString);
    }  
    return FileName;
}  

/**
 * Open BOOT's EFI installation directory relative to the loaded image.
 * Retain an existing root handle when present; propagate failure if the
 * root or child cannot be opened, leaving SelfDir unusable.
 */
static EFI_STATUS FinishInitRefitLib(VOID)
{
    EFI_STATUS  Status;

    if (SelfRootDir == NULL) {
        SelfRootDir = LibOpenRoot(SelfLoadedImage->DeviceHandle);
        if (SelfRootDir == NULL) {
            CheckError(EFI_LOAD_ERROR, L"while (re)opening our installation volume");
            return EFI_LOAD_ERROR;
        }
    }

    Status = refit_call5_wrapper(SelfRootDir->Open, SelfRootDir, &SelfDir, SelfDirPath, EFI_FILE_MODE_READ, 0);
    if (CheckFatalError(Status, L"while opening our installation directory"))
        return EFI_LOAD_ERROR;

    return EFI_SUCCESS;
}

/*
 * Establishes the library's own image, device and volume context from the
 * firmware handle supplied at entry. File/protocol operations elsewhere
 * require successful initialisation and valid owned handles.
 */
EFI_STATUS InitRefitLib(IN EFI_HANDLE ImageHandle)
{
    EFI_STATUS  Status;
    CHAR16      *DevicePathAsString, *Temp = NULL;

    SelfImageHandle = ImageHandle;
    Status = refit_call3_wrapper(BS->HandleProtocol, SelfImageHandle, &LoadedImageProtocol, (VOID **) &SelfLoadedImage);
    if (CheckFatalError(Status, L"while getting a LoadedImageProtocol handle"))
        return EFI_LOAD_ERROR;

    DevicePathAsString = DevicePathToStr(SelfLoadedImage->FilePath);
    GlobalConfig.SelfDevicePath = FileDevicePath(SelfLoadedImage->DeviceHandle, DevicePathAsString);
    CleanUpPathNameSlashes(DevicePathAsString);
    MyFreePool(SelfDirPath);
    Temp = FindPath(DevicePathAsString);
    SelfDirPath = SplitDeviceString(Temp);
    MyFreePool(DevicePathAsString);
    MyFreePool(Temp);

    return FinishInitRefitLib();
}

/*
 * Before EFI handoff, close each open root directory and invalidate
 * firmware handle references. Volume records and device paths remain
 * allocated so that ReinitVolumes can restore the firmware-facing state if
 * the child returns. This is not permanent destruction of the inventory.
 */
static VOID UninitVolumes(VOID)
{
    REFIT_VOLUME            *Volume;
    UINTN                   VolumeIndex;

    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
        Volume = Volumes[VolumeIndex];

        if (Volume->RootDir != NULL) {
            refit_call1_wrapper(Volume->RootDir->Close, Volume->RootDir);
            Volume->RootDir = NULL;
        }

        Volume->DeviceHandle = NULL;
        Volume->BlockIO = NULL;
        Volume->WholeDiskBlockIO = NULL;
    }
}  

/*
 * Resolve retained device paths again after a child image returns: firmware
 * controller bindings and handles may have changed during StartImage. A
 * failed path lookup must not be treated as a still-usable prior RootDir or
 * block protocol.
 */
VOID ReinitVolumes(VOID)
{
    EFI_STATUS              Status;
    REFIT_VOLUME            *Volume;
    UINTN                   VolumeIndex;
    EFI_DEVICE_PATH         *RemainingDevicePath;
    EFI_HANDLE              DeviceHandle, WholeDiskHandle;

    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
        Volume = Volumes[VolumeIndex];

        if (Volume->DevicePath != NULL) {

            RemainingDevicePath = Volume->DevicePath;
            Status = refit_call3_wrapper(BS->LocateDevicePath, &BlockIoProtocol, &RemainingDevicePath, &DeviceHandle);

            if (EFI_ERROR(Status)) {
                CheckError(Status, L"from LocateDevicePath");
            } else {
                Volume->DeviceHandle = DeviceHandle;

                Volume->RootDir = LibOpenRoot(Volume->DeviceHandle);
            }
        }

        if (Volume->WholeDiskDevicePath != NULL) {

            RemainingDevicePath = Volume->WholeDiskDevicePath;
            Status = refit_call3_wrapper(BS->LocateDevicePath, &BlockIoProtocol, &RemainingDevicePath, &WholeDiskHandle);

            if (!EFI_ERROR(Status)) {

                Status = refit_call3_wrapper(BS->HandleProtocol, WholeDiskHandle, &BlockIoProtocol,
                                             (VOID **) &Volume->WholeDiskBlockIO);
                if (EFI_ERROR(Status)) {
                    Volume->WholeDiskBlockIO = NULL;
                    CheckError(Status, L"from HandleProtocol");
                }
            } else
                CheckError(Status, L"from LocateDevicePath");
        }
    }
}  

/*
 * Releases BOOT-managed firmware resources ahead of an external handoff or
 * reset. The corresponding reinitialisation path is needed if control
 * returns rather than leaving the application permanently.
 */
VOID UninitRefitLib(VOID)
{

    if (StrCmp(ST->FirmwareVendor, L"Das U-Boot") != 0) {
        StopLogging();
    }

    if(SelfRootDir == SelfVolume->RootDir)
        SelfRootDir=0;

    UninitVolumes();

    if (SelfDir != NULL) {
        refit_call1_wrapper(SelfDir->Close, SelfDir);
        SelfDir = NULL;
    }

    if (SelfRootDir != NULL) {
       refit_call1_wrapper(SelfRootDir->Close, SelfRootDir);
       SelfRootDir = NULL;
    }
}  

/**
 * Refresh volume records before reopening BOOT's own directory. Older
 * firmware may alias SelfRootDir to SelfVolume's retained root. Restart
 * logging only when the directory is successfully restored.
 */
EFI_STATUS ReinitRefitLib(VOID)
{
    EFI_STATUS Status;

    ReinitVolumes();

    if ((ST->Hdr.Revision >> 16) == 1) {

       if (SelfVolume != NULL && SelfVolume->RootDir != NULL)
          SelfRootDir = SelfVolume->RootDir;
    }  

    Status = FinishInitRefitLib();
    if (EFI_ERROR(Status)) {

        GlobalConfig.LogLevel = 0;
    } else {
        StartLogging(TRUE);
    }
    return Status;
}

EFI_STATUS CreateVarsDir(VOID) {
    EFI_STATUS       Status = EFI_SUCCESS;
    EFI_FILE_HANDLE  EspRootDir;

    if (gVarsDir == NULL) {
        LOG(1, LOG_LINE_NORMAL, L"Trying to create a 'vars' directory in which to hold variables");
        Status = refit_call5_wrapper(SelfDir->Open, SelfDir, &gVarsDir, L"vars",
                                     EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                                     EFI_FILE_DIRECTORY);
        if (EFI_ERROR(Status)) {
            Status = egFindESP(&EspRootDir);
            if (Status == EFI_SUCCESS) {
                LOG(1, LOG_LINE_NORMAL,
                    L"Trying to create a 'boot-vars' directory on the ESP in which to hold variables");
                Status = refit_call5_wrapper(EspRootDir->Open, EspRootDir, &gVarsDir, L"boot-vars",
                                             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                                             EFI_FILE_DIRECTORY);
            }
        }
    }
    if (EFI_ERROR(Status)) {
        GlobalConfig.UseNvram = TRUE;
        LOG(1, LOG_LINE_NORMAL, L"Unable to create a directory in which to hold variables; error %d", Status);
        LOG(1, LOG_LINE_NORMAL, L"Falling back to NVRAM-based storage");
    }
    return Status;
}  

/**
 * BOOT-owned variables use disk storage when UseNvram is false. Failure opening
 * the variables directory falls back to firmware. Once the directory opens, a
 * missing file is returned as a read failure. GlobalGuid variables such as
 * BootOrder always use firmware.
 *
 * Success transfers a pool buffer to the caller. The size output is genuinely
 * optional: an internal variable receives the byte count when it is omitted.
 * An error clears the caller's buffer and any supplied size; the firmware
 * allocation is freed, and the caller never owns a failed result.
 */
EFI_STATUS EfivarGetRaw(IN EFI_GUID *vendor, IN CHAR16 *name, OUT CHAR8 **buffer, OUT UINTN *size OPTIONAL) {
    UINT8 *buf = NULL;
    UINTN l;
    EFI_STATUS Status;
    BOOLEAN ReadFromNvram = TRUE;
    UINTN IgnoredSize = 0;

    if (!vendor || !name || !buffer)
        return EFI_INVALID_PARAMETER;
    *buffer = NULL;
    if (!size)
        size = &IgnoredSize;
    *size = 0;

    if ((GlobalConfig.UseNvram == FALSE) && GuidsAreEqual(vendor, &BootGuid)) {
        ReadFromNvram = FALSE;
    }
    LOG(3, LOG_LINE_NORMAL, L"Getting EFI variable '%s' from %s", name,
        ReadFromNvram ? L"NVRAM" : L"disk");

    if (!ReadFromNvram) {
        Status = CreateVarsDir();
        if (Status == EFI_SUCCESS) {
            Status = egLoadFile(gVarsDir, name, &buf, size);
        } else {
            ReadFromNvram = TRUE;
        }
    }
    if (ReadFromNvram) {
        l = sizeof(CHAR16 *) * EFI_MAXIMUM_VARIABLE_SIZE;
        buf = AllocatePool(l);
        if (!buf) {
            *buffer = NULL;
            return EFI_OUT_OF_RESOURCES;
        }
        Status = refit_call5_wrapper(RT->GetVariable, name, vendor, NULL, &l, buf);
    }

    if (EFI_ERROR(Status) == EFI_SUCCESS) {
        *buffer = (CHAR8*) buf;
        if ((size) && ReadFromNvram)
            *size = l;
    } else {

        LOG(3, LOG_LINE_NORMAL, L"Error retrieving EFI variable '%s'", name);
        MyFreePool(buf);
        *buffer = NULL;
        *size = 0;
    }
    return Status;
}  

/**
 * Read the old value first. Equal lengths and equal payload bytes suppress a
 * redundant write. For firmware variables, persistent adds
 * EFI_VARIABLE_NON_VOLATILE to boot-service and runtime access. The BootGuid disk
 * policy is the same as in EfivarGetRaw.
 */
EFI_STATUS EfivarSetRaw(EFI_GUID *vendor, CHAR16 *name, CHAR8 *buf, UINTN size, BOOLEAN persistent) {
    UINT32      flags;
    EFI_STATUS  Status = EFI_SUCCESS, OldStatus;
    CHAR8       *OldBuf;
    UINTN       OldSize;
    BOOLEAN     WriteToNvram = TRUE;

    if ((GlobalConfig.UseNvram == FALSE) && GuidsAreEqual(vendor, &BootGuid))
        WriteToNvram = FALSE;

    OldStatus = EfivarGetRaw(vendor, name, &OldBuf, &OldSize);
    LOG(2, LOG_LINE_NORMAL, L"Saving EFI variable '%s' to %s", name,
        WriteToNvram ? L"NVRAM" : L"disk");
    if ((EFI_ERROR(OldStatus)) || (size != OldSize) || (CompareMem(buf, OldBuf, size) != 0)) {
        if (!WriteToNvram) {
            Status = CreateVarsDir();
            if (Status == EFI_SUCCESS) {
                Status = egSaveFile(gVarsDir, name, (UINT8 *) buf, size);
            } else {
                WriteToNvram = TRUE;
            }
        }
        if (WriteToNvram) {
            flags = EFI_VARIABLE_BOOTSERVICE_ACCESS|EFI_VARIABLE_RUNTIME_ACCESS;
            if (persistent)
                flags |= EFI_VARIABLE_NON_VOLATILE;

            Status = refit_call5_wrapper(RT->SetVariable, name, vendor, flags, size, buf);
        }
    } else {
        LOG(2, LOG_LINE_NORMAL, L"Not writing variable '%s'; it's unchanged", name);
    }
    if (OldStatus == EFI_SUCCESS) {
        LOG(4, LOG_LINE_NORMAL, L"Freeing OldBuf");
        MyFreePool(OldBuf);
        LOG(4, LOG_LINE_NORMAL, L"Have freed OldBuf");
    }
    if (EFI_ERROR(Status))
        LOG(1, LOG_LINE_NORMAL, L"Error %d when writing EFI variable '%s'", Status, name);

    return Status;
}  

/*
 * Expand a pointer vector in groups of sixteen slots. The vector owns its
 * pointer array, not necessarily the objects it references; callers determine
 * element lifetime. Grow with a separate AllocatePool/copy because GNU-EFI
 * ReallocatePool may free the old storage even when the new allocation fails.
 * On failure the old vector and count remain intact. This legacy VOID API
 * cannot notify callers that the attempted append was not performed.
 */
VOID AddListElement(IN OUT VOID ***ListPtr, IN OUT UINTN *ElementCount, IN VOID *NewElement)
{
    UINTN AllocateCount;
    VOID **NewList;

    if ((*ElementCount & 15) == 0) {
        AllocateCount = *ElementCount + 16;
        if (AllocateCount < *ElementCount ||
            AllocateCount > (~(UINTN)0) / sizeof(VOID *)) {
            LOG(1, LOG_LINE_NORMAL, L"Pointer list capacity overflow");
            return;
        }

        // Allocate first: GNU-EFI ReallocatePool may release the old buffer
        // even when its replacement allocation fails.
        NewList = AllocatePool(sizeof(VOID *) * AllocateCount);
        if (NewList == NULL) {
            LOG(1, LOG_LINE_NORMAL, L"Out of memory while growing a BOOT pointer list");
            return;
        }
        if (*ElementCount != 0) {
            CopyMem(NewList, *ListPtr, sizeof(VOID *) * (*ElementCount));
            MyFreePool(*ListPtr);
        }
        *ListPtr = NewList;
    }
    (*ListPtr)[*ElementCount] = NewElement;
    (*ElementCount)++;
}  

/*
 * Free each pointer as an independently pool-allocated object and finally
 * free the vector storage. This is not suitable for lists of static or
 * borrowed entries. The current routine does not reset its caller's pointer
 * or count; reuse without resetting those values risks a second release.
 */
VOID FreeList(IN OUT VOID ***ListPtr, IN OUT UINTN *ElementCount)
{
    UINTN i;

    if ((*ElementCount > 0) && (**ListPtr != NULL)) {
        for (i = 0; i < *ElementCount; i++) {

            MyFreePool((*ListPtr)[i]);
        }
        MyFreePool(*ListPtr);
    }
}  

static CHAR16 *FSTypeName(IN UINT32 TypeCode) {
    CHAR16 *retval = NULL;

    switch (TypeCode) {
        case FS_TYPE_WHOLEDISK:
            retval = L" whole disk";
            break;
        case FS_TYPE_FAT:
            retval = L" FAT";
            break;
        case FS_TYPE_HFSPLUS:
            retval = L" HFS+";
            break;
        case FS_TYPE_EXT2:
            retval = L" ext2";
            break;
        case FS_TYPE_EXT3:
            retval = L" ext3";
            break;
        case FS_TYPE_EXT4:
            retval = L" ext4";
            break;
        case FS_TYPE_REISERFS:
            retval = L" ReiserFS";
            break;
        case FS_TYPE_BTRFS:
            retval = L" Btrfs";
            break;
        case FS_TYPE_XFS:
            retval = L" XFS";
            break;
        case FS_TYPE_JFS:
            retval = L" JFS";
            break;
        case FS_TYPE_ISO9660:
            retval = L" ISO-9660";
            break;
        case FS_TYPE_NTFS:
            retval = L" NTFS";
            break;
        default:
            retval = L"";
            break;
    }  
    return retval;
}  

/*
 * Ask the firmware Simple File System protocol for a volume label when
 * RootDir is present, replacing any earlier owned FsName only when a
 * nonempty label is returned. This label is descriptive firmware metadata,
 * not a trustworthy filesystem identity or proof of file readability.
 */
static VOID SetFilesystemName(REFIT_VOLUME *Volume) {
    EFI_FILE_SYSTEM_INFO    *FileSystemInfoPtr = NULL;

    if ((Volume) && (Volume->RootDir != NULL)) {
        FileSystemInfoPtr = LibFileSystemInfo(Volume->RootDir);
     }

    if ((FileSystemInfoPtr != NULL) &&
        (StrLen(FileSystemInfoPtr->VolumeLabel) > 0)) {
        if (Volume->FsName) {
            MyFreePool(Volume->FsName);
            Volume->FsName = NULL;
        }
        Volume->FsName = StrDuplicate(FileSystemInfoPtr->VolumeLabel);
    }
    MyFreePool(FileSystemInfoPtr);
}  

static VOID SetFilesystemData(IN UINT8 *Buffer, IN UINTN BufferSize, IN OUT REFIT_VOLUME *Volume) {
    UINT32       *Ext2Incompat, *Ext2Compat;
    UINT16       *Magic16;
    char         *MagicString;

    LOG(2, LOG_LINE_NORMAL, L"Identifying filesystem types....");
    if ((Buffer != NULL) && (Volume != NULL)) {
        SetMem(&(Volume->VolUuid), sizeof(EFI_GUID), 0);
        Volume->FSType = FS_TYPE_UNKNOWN;

        if (BufferSize >= (1024 + 100)) {
            Magic16 = (UINT16*) (Buffer + 1024 + 56);
            if (*Magic16 == EXT2_SUPER_MAGIC) {  
                Ext2Compat = (UINT32*) (Buffer + 1024 + 92);
                Ext2Incompat = (UINT32*) (Buffer + 1024 + 96);
                if ((*Ext2Incompat & 0x0040) || (*Ext2Incompat & 0x0200)) {  
                    LOG(4, LOG_LINE_NORMAL, L"Found ext4fs");
                    Volume->FSType = FS_TYPE_EXT4;
                } else if (*Ext2Compat & 0x0004) {  
                    LOG(4, LOG_LINE_NORMAL, L"Found ext3fs");
                    Volume->FSType = FS_TYPE_EXT3;
                } else {  
                    LOG(4, LOG_LINE_NORMAL, L"Found ext2fs");
                    Volume->FSType = FS_TYPE_EXT2;
                }
                MyCopyMem(&(Volume->VolUuid), Buffer + 1024 + 104, sizeof(EFI_GUID));
                return;
            }
        }  

        if (BufferSize >= (65536 + 100)) {
            MagicString = (char*) (Buffer + 65536 + 52);
            if ((CompareMem(MagicString, REISERFS_SUPER_MAGIC_STRING, 8) == 0) ||
                (CompareMem(MagicString, REISER2FS_SUPER_MAGIC_STRING, 9) == 0) ||
                (CompareMem(MagicString, REISER2FS_JR_SUPER_MAGIC_STRING, 9) == 0)) {
                    LOG(4, LOG_LINE_NORMAL, L"Found ReiserFS");
                    Volume->FSType = FS_TYPE_REISERFS;
                    MyCopyMem(&(Volume->VolUuid), Buffer + 65536 + 84, sizeof(EFI_GUID));
                    return;
            }  
        }  

        if (BufferSize >= (65536 + 64 + 8)) {
            MagicString = (char*) (Buffer + 65536 + 64);
            if (CompareMem(MagicString, BTRFS_SIGNATURE, 8) == 0) {
                LOG(4, LOG_LINE_NORMAL, L"Found Btrfs");
                Volume->FSType = FS_TYPE_BTRFS;
                return;
            }  
        }  

        if (BufferSize >= 512) {
            MagicString = (char*) Buffer;
            if (CompareMem(MagicString, XFS_SIGNATURE, 4) == 0) {
                LOG(4, LOG_LINE_NORMAL, L"Found XFS");
                Volume->FSType = FS_TYPE_XFS;
                return;
            }
        }  

        if (BufferSize >= (32768 + 4)) {
            MagicString = (char*) (Buffer + 32768);
            if (CompareMem(MagicString, JFS_SIGNATURE, 4) == 0) {
                LOG(4, LOG_LINE_NORMAL, L"Found JFS");
                Volume->FSType = FS_TYPE_JFS;
                return;
            }
        }  

        if (BufferSize >= (1024 + 2)) {
            Magic16 = (UINT16*) (Buffer + 1024);
            if ((*Magic16 == HFSPLUS_MAGIC1) || (*Magic16 == HFSPLUS_MAGIC2)) {
                LOG(4, LOG_LINE_NORMAL, L"Found HFS+");
                Volume->FSType = FS_TYPE_HFSPLUS;
                return;
            }
        }  

        if (BufferSize >= 512) {

           Magic16 = (UINT16*) (Buffer + 510);
           if (*Magic16 == FAT_MAGIC) {
               MagicString = (char*) Buffer;
               if (CompareMem(MagicString + 3, NTFS_SIGNATURE, 8) == 0) {
                   LOG(4, LOG_LINE_NORMAL, L"Found NTFS");
                   Volume->FSType = FS_TYPE_NTFS;
                   MyCopyMem(&(Volume->VolUuid), Buffer + 0x48, sizeof(UINT64));
               } else if ((CompareMem(MagicString + 0x36, FAT12_SIGNATURE, 8) == 0) ||
                          (CompareMem(MagicString + 0x36, FAT16_SIGNATURE, 8) == 0)) {
                   LOG(4, LOG_LINE_NORMAL, L"Found FAT12 or FAT16");
                   Volume->FSType = FS_TYPE_FAT;
                   MyCopyMem(&(Volume->VolUuid), Buffer + 0x27, sizeof(UINT32));
               } else if (CompareMem(MagicString + 0x52, FAT32_SIGNATURE, 8) == 0) {
                   LOG(4, LOG_LINE_NORMAL, L"Found FAT32");
                   Volume->FSType = FS_TYPE_FAT;
                   MyCopyMem(&(Volume->VolUuid), Buffer + 0x43, sizeof(UINT32));
               } else if (!Volume->BlockIO->Media->LogicalPartition) {
                   LOG(4, LOG_LINE_NORMAL, L"Found disk boot sector/MBR");
                   Volume->FSType = FS_TYPE_WHOLEDISK;
               }  
               return;
            }  
        }  

        if (Volume->BlockIO->Media->BlockSize == 2048) {
            LOG(4, LOG_LINE_NORMAL, L"Found ISO-9660");
            Volume->FSType = FS_TYPE_ISO9660;
            return;
        }
    }  
}  

/**
 * Use a fixed-size Block I/O sample to classify legacy boot candidates.
 * Absent media, unsupported geometry and failed reads retain the explicit
 * nonbootable default; signatures are not proof of EFI execution.
 */
static VOID ScanVolumeBootcode(REFIT_VOLUME *Volume, BOOLEAN *Bootable)
{
    EFI_STATUS              Status;
    UINT8                   Buffer[SAMPLE_SIZE];

    LOG(2, LOG_LINE_NORMAL, L"Entering ScanVolumeBootcode()");
    Volume->HasBootCode = FALSE;
    Volume->OSIconName = NULL;
    Volume->OSName = NULL;
    *Bootable = FALSE;

    if (Volume->BlockIO == NULL)
        return;
    if (Volume->BlockIO->Media->BlockSize > SAMPLE_SIZE)
        return;    

    Status = refit_call5_wrapper(Volume->BlockIO->ReadBlocks,
                                 Volume->BlockIO, Volume->BlockIO->Media->MediaId,
                                 Volume->BlockIOOffset, SAMPLE_SIZE, Buffer);
    if (EFI_ERROR(Status)) {
        LOG(1, LOG_LINE_NORMAL, L"Error %d reading boot sector of '%s'", Status, Volume->VolName);
    } else {
        SetFilesystemData(Buffer, SAMPLE_SIZE, Volume);
    }
#if defined(EFI32) || defined(EFIX64)
    MBR_PARTITION_INFO      *MbrTable;
    BOOLEAN                 MbrTableFound = FALSE;
    UINTN                   i;

    if ((Status == EFI_SUCCESS) && (GlobalConfig.LegacyType == LEGACY_TYPE_MAC)) {
        if ((*((UINT16 *)(Buffer + 510)) == 0xaa55 && Buffer[0] != 0) &&
            (FindMem(Buffer, 512, "EXFAT", 5) == -1)) {
                *Bootable = TRUE;
                Volume->HasBootCode = TRUE;
        }

        if (CompareMem(Buffer + 2, "LILO", 4) == 0 ||
            CompareMem(Buffer + 6, "LILO", 4) == 0 ||
            CompareMem(Buffer + 3, "SYSLINUX", 8) == 0 ||
            FindMem(Buffer, SECTOR_SIZE, "ISOLINUX", 8) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"linux";
            Volume->OSName = L"Linux (Legacy)";

        } else if (FindMem(Buffer, 512, "Geom\0Hard Disk\0Read\0 Error", 26) >= 0) {    
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"grub,linux";
            Volume->OSName = L"Linux (Legacy)";

        } else if ((*((UINT32 *)(Buffer + 502)) == 0 &&
                    *((UINT32 *)(Buffer + 506)) == 50000 &&
                    *((UINT16 *)(Buffer + 510)) == 0xaa55) ||
                    FindMem(Buffer, SECTOR_SIZE, "Starting the BTX loader", 23) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"freebsd";
            Volume->OSName = L"FreeBSD (Legacy)";

        } else if ((*((UINT16 *)(Buffer + 510)) == 0xaa55) &&
                   (FindMem(Buffer, SECTOR_SIZE, "Boot loader too large", 21) >= 0) &&
                   (FindMem(Buffer, SECTOR_SIZE, "I/O error loading boot loader", 29) >= 0))  {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"freebsd";
            Volume->OSName = L"FreeBSD (Legacy)";

        } else if (FindMem(Buffer, 512, "!Loading", 8) >= 0 ||
                   FindMem(Buffer, SECTOR_SIZE, "/cdboot\0/CDBOOT\0", 16) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"openbsd";
            Volume->OSName = L"OpenBSD (Legacy)";

        } else if (FindMem(Buffer, 512, "Not a bootxx image", 18) >= 0 ||
                   *((UINT32 *)(Buffer + 1028)) == 0x7886b6d1) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"netbsd";
            Volume->OSName = L"NetBSD (Legacy)";

        } else if (FindMem(Buffer, SECTOR_SIZE, "NTLDR", 5) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"win";
            Volume->OSName = L"Windows (Legacy)";

        } else if (FindMem(Buffer, SECTOR_SIZE, "BOOTMGR", 7) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"win8,win";
            Volume->OSName = L"Windows (Legacy)";

        } else if (FindMem(Buffer, 512, "CPUBOOT SYS", 11) >= 0 ||
                   FindMem(Buffer, 512, "KERNEL  SYS", 11) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"freedos";
            Volume->OSName = L"FreeDOS (Legacy)";

        } else if (FindMem(Buffer, 512, "OS2LDR", 6) >= 0 ||
                   FindMem(Buffer, 512, "OS2BOOT", 7) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"ecomstation";
            Volume->OSName = L"eComStation (Legacy)";

        } else if (FindMem(Buffer, 512, "Be Boot Loader", 14) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"beos";
            Volume->OSName = L"BeOS (Legacy)";

        } else if (FindMem(Buffer, 512, "yT Boot Loader", 14) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"zeta,beos";
            Volume->OSName = L"ZETA (Legacy)";

        } else if (FindMem(Buffer, 512, "\x04" "beos\x06" "system\x05" "zbeos", 18) >= 0 ||
                   FindMem(Buffer, 512, "\x06" "system\x0c" "haiku_loader", 20) >= 0) {
            Volume->HasBootCode = TRUE;
            Volume->OSIconName = L"haiku,beos";
            Volume->OSName = L"Haiku (Legacy)";

        }

        LOG(1, LOG_LINE_NORMAL, L"Result of bootcode detection: %s %s (%s)",
            Volume->HasBootCode ? L"bootable" : L"non-bootable", Volume->OSName,
            Volume->OSIconName);

        if (FindMem(Buffer, 512, "Non-system disk", 15) >= 0)
            Volume->HasBootCode = FALSE;

        if (FindMem(Buffer, 512, "This is not a bootable disk", 27) >= 0)
            Volume->HasBootCode = FALSE;

        if (FindMem(Buffer, 512, "Press any key to restart", 24) >= 0)
            Volume->HasBootCode = FALSE;

        if (*((UINT16 *)(Buffer + 510)) == 0xaa55) {
            MbrTable = (MBR_PARTITION_INFO *)(Buffer + 446);
            for (i = 0; i < 4; i++)
                if (MbrTable[i].StartLBA && MbrTable[i].Size)
                    MbrTableFound = TRUE;
            for (i = 0; i < 4; i++)
                if (MbrTable[i].Flags != 0x00 && MbrTable[i].Flags != 0x80)
                    MbrTableFound = FALSE;
            if (MbrTableFound) {
                Volume->MbrPartitionTable = AllocatePool(4 * 16);
                MyCopyMem(Volume->MbrPartitionTable, MbrTable, 4 * 16);
            }
        }

    }
#endif
}  

VOID SetVolumeBadgeIcon(REFIT_VOLUME *Volume)
{
    if (Volume == NULL)
        return;

    if (GlobalConfig.HideUIFlags & HIDEUI_FLAG_BADGES)
        return;

    if (Volume->VolBadgeImage == NULL) {
        Volume->VolBadgeImage = egLoadIconAnyType(Volume->RootDir, L"",
                                                  L".VolumeBadge",
                                                  GlobalConfig.IconSizes[ICON_SIZE_BADGE]);
    }

    if (Volume->VolBadgeImage == NULL) {
        switch (Volume->DiskKind) {
            case DISK_KIND_INTERNAL:
                Volume->VolBadgeImage = BuiltinIcon(BUILTIN_ICON_VOL_INTERNAL);
                break;
            case DISK_KIND_EXTERNAL:
                Volume->VolBadgeImage = BuiltinIcon(BUILTIN_ICON_VOL_EXTERNAL);
                break;
            case DISK_KIND_OPTICAL:
                Volume->VolBadgeImage = BuiltinIcon(BUILTIN_ICON_VOL_OPTICAL);
                break;
            case DISK_KIND_NET:
                Volume->VolBadgeImage = BuiltinIcon(BUILTIN_ICON_VOL_NET);
                break;
        }  
    }
}  

static CHAR16 *SizeInIEEEUnits(UINT64 SizeInBytes) {
    UINT64 SizeInIeee;
    UINTN Index = 0, NumPrefixes;
    CHAR16 *Units = NULL, *Prefixes = L" KMGTPEZ";
    CHAR16 *TheValue;

    NumPrefixes = StrLen(Prefixes);
    SizeInIeee = SizeInBytes;
    while ((SizeInIeee > 1024) && (Index < (NumPrefixes - 1))) {
        Index++;
        SizeInIeee /= 1024;
    }  
    if (Prefixes[Index] == ' ') {
        Units = StrDuplicate(L"-byte");
    } else {
        Units = StrDuplicate(L"  iB");
        Units[1] = Prefixes[Index];
    }  
    TheValue = PoolPrint(L"%ld%s", SizeInIeee, Units);
    MyFreePool(Units);
    return TheValue;
}  

static CHAR16 *GetVolumeName(REFIT_VOLUME *Volume) {
    EFI_FILE_SYSTEM_INFO    *FileSystemInfoPtr = NULL;
    CHAR16                  *FoundName = NULL;
    CHAR16                  *SISize, *TypeName;

    if ((Volume->FsName) && (StrLen(Volume->FsName) > 0)) {
        FoundName = StrDuplicate(Volume->FsName);
        LOG(3, LOG_LINE_NORMAL, L"Setting volume name to filesystem name of '%s'", FoundName);
    }

    if ((FoundName == NULL) && (Volume->PartName) && (StrLen(Volume->PartName) > 0) &&
        !IsIn(Volume->PartName, IGNORE_PARTITION_NAMES)) {
        FoundName = StrDuplicate(Volume->PartName);
        LOG(3, LOG_LINE_NORMAL, L"Setting volume name to partition name of '%s'", FoundName);
    }  

    if (FoundName == NULL) {
        if (Volume->RootDir != NULL) {
            FileSystemInfoPtr = LibFileSystemInfo(Volume->RootDir);
        }
        if (FileSystemInfoPtr != NULL) {
            SISize = SizeInIEEEUnits(FileSystemInfoPtr->VolumeSize);
            FoundName = PoolPrint(L"%s%s volume", SISize, FSTypeName(Volume->FSType));
            MyFreePool(SISize);
            LOG(3, LOG_LINE_NORMAL, L"Setting volume name to filesystem description of '%s'", FoundName);
            MyFreePool(FileSystemInfoPtr);
        }
    }  

    if (FoundName == NULL) {
        TypeName = FSTypeName(Volume->FSType);  
        if (StrLen(TypeName) > 0)
            FoundName = PoolPrint(L"%s volume", TypeName);
        else
            FoundName = StrDuplicate(L"unknown volume");
        LOG(3, LOG_LINE_NORMAL, L"Setting volume name to generic description of '%s'", FoundName);
    }  

    return FoundName;
}  

/**
 * Use a hard-drive device path's GUID signature to find the GPT entry by unique
 * partition ID. Copy out the human name and type GUID before freeing the
 * temporary entry. The architecture's root type is eligible for automatic Linux
 * selection only when the no-automount bit is clear.
 */
static VOID SetPartGuidAndName(REFIT_VOLUME *Volume, EFI_DEVICE_PATH_PROTOCOL *DevicePath) {
    HARDDRIVE_DEVICE_PATH    *HdDevicePath;
    GPT_ENTRY                *PartInfo;

    if ((Volume == NULL) || (DevicePath == NULL))
        return;

    if ((DevicePath->Type == MEDIA_DEVICE_PATH) && (DevicePath->SubType == MEDIA_HARDDRIVE_DP)) {
        HdDevicePath = (HARDDRIVE_DEVICE_PATH*) DevicePath;
        if (HdDevicePath->SignatureType == SIGNATURE_TYPE_GUID) {
            Volume->PartGuid = *((EFI_GUID*) HdDevicePath->Signature);
            PartInfo = FindPartWithGuid(&(Volume->PartGuid));
            if (PartInfo) {
                Volume->PartName = StrDuplicate(PartInfo->name);
                MyCopyMem(&(Volume->PartTypeGuid), PartInfo->type_guid, sizeof(EFI_GUID));
                if (GuidsAreEqual(&(Volume->PartTypeGuid), &gFreedesktopRootGuid) &&
                        ((PartInfo->attributes & GPT_NO_AUTOMOUNT) == 0)) {
                    GlobalConfig.DiscoveredRoot = Volume;
                }  
                Volume->IsMarkedReadOnly = ((PartInfo->attributes & GPT_READ_ONLY) > 0);
                MyFreePool(PartInfo);
            }  
        } else {

            Volume->PartGuid = StringAsGuid(L"92a6c61f-7130-49b9-b05c-8d7e7b039127");
        }  
    }  
}  

static BOOLEAN HasWindowsBiosBootFiles(REFIT_VOLUME *Volume) {
    BOOLEAN FilesFound = TRUE;

    if (Volume->RootDir != NULL) {
        FilesFound = FileExists(Volume->RootDir, L"NTLDR") ||   
                     FileExists(Volume->RootDir, L"bootmgr");   
    }  
    return FilesFound;
}  

/*
 * Derives partition/device information from EFI handles and Block I/O.
 * A missing protocol or a recognised-but-unreadable filesystem is not evidence
 * of a valid OS loader; downstream scanning must require readable RootDir.
 */
VOID ScanVolume(REFIT_VOLUME *Volume)
{
    EFI_STATUS              Status;
    EFI_DEVICE_PATH         *DevicePath, *NextDevicePath;
    EFI_DEVICE_PATH         *DiskDevicePath, *RemainingDevicePath;
    EFI_HANDLE              WholeDiskHandle;
    UINTN                   PartialLength;
    BOOLEAN                 Bootable;

    LOG(2, LOG_LINE_NORMAL, L"Entering ScanVolume()");

    Volume->DevicePath = DuplicateDevicePath(DevicePathFromHandle(Volume->DeviceHandle));

    Volume->DiskKind = DISK_KIND_INTERNAL;   

    Status = refit_call3_wrapper(BS->HandleProtocol, Volume->DeviceHandle, &BlockIoProtocol, (VOID **) &(Volume->BlockIO));
    if (EFI_ERROR(Status)) {
        Volume->BlockIO = NULL;
        Print(L"Warning: Can't get BlockIO protocol.\n");
        LOG(1, LOG_LINE_NORMAL, L"Warning: Can't get BlockIO protocol in ScanVolume()");
    } else {
        if (Volume->BlockIO->Media->BlockSize == 2048)
            Volume->DiskKind = DISK_KIND_OPTICAL;
    }

    Bootable = FALSE;
    ScanVolumeBootcode(Volume, &Bootable);

    DevicePath = Volume->DevicePath;
    while (DevicePath != NULL && !IsDevicePathEndType(DevicePath)) {
        NextDevicePath = NextDevicePathNode(DevicePath);

        if (DevicePathType(DevicePath) == MEDIA_DEVICE_PATH) {
           SetPartGuidAndName(Volume, DevicePath);
        }
        if (DevicePathType(DevicePath) == MESSAGING_DEVICE_PATH &&
            (DevicePathSubType(DevicePath) == MSG_USB_DP ||
             DevicePathSubType(DevicePath) == MSG_USB_CLASS_DP ||
             DevicePathSubType(DevicePath) == MSG_1394_DP ||
             DevicePathSubType(DevicePath) == MSG_FIBRECHANNEL_DP))
            Volume->DiskKind = DISK_KIND_EXTERNAL;     
        if (DevicePathType(DevicePath) == MEDIA_DEVICE_PATH &&
            DevicePathSubType(DevicePath) == MEDIA_CDROM_DP) {
            Volume->DiskKind = DISK_KIND_OPTICAL;      
            Bootable = TRUE;
        }

        if (DevicePathType(DevicePath) == MESSAGING_DEVICE_PATH) {

            PartialLength = (UINT8 *)NextDevicePath - (UINT8 *)(Volume->DevicePath);
            DiskDevicePath = (EFI_DEVICE_PATH *)AllocatePool(PartialLength + sizeof(EFI_DEVICE_PATH));
            MyCopyMem(DiskDevicePath, Volume->DevicePath, PartialLength);
            MyCopyMem((UINT8 *)DiskDevicePath + PartialLength, EndDevicePath, sizeof(EFI_DEVICE_PATH));

            RemainingDevicePath = DiskDevicePath;
            Status = refit_call3_wrapper(BS->LocateDevicePath, &BlockIoProtocol,
                                         &RemainingDevicePath, &WholeDiskHandle);
            MyFreePool(DiskDevicePath);

            if (!EFI_ERROR(Status)) {

                Status = refit_call3_wrapper(BS->HandleProtocol, WholeDiskHandle,
                                             &DevicePathProtocol, (VOID **) &DiskDevicePath);
                if (EFI_ERROR(Status)) {
                    LOG(1, LOG_LINE_NORMAL, L"Could not get DiskDevicePath for volume");
                } else {
                    Volume->WholeDiskDevicePath = DuplicateDevicePath(DiskDevicePath);
                }

                Status = refit_call3_wrapper(BS->HandleProtocol, WholeDiskHandle, &BlockIoProtocol,
                                             (VOID **) &Volume->WholeDiskBlockIO);
                if (EFI_ERROR(Status)) {
                    LOG(1, LOG_LINE_NORMAL, L"Could not get WholeDiskBlockIO for volume");
                    Volume->WholeDiskBlockIO = NULL;

                } else {

                    if (Volume->WholeDiskBlockIO->Media->BlockSize == 2048)
                        Volume->DiskKind = DISK_KIND_OPTICAL;
                }
            } else {
                LOG(1, LOG_LINE_NORMAL, L"Could not locate device path for volume");
            }
        }

        DevicePath = NextDevicePath;
    }  

    if (!Bootable) {
        if (Volume->HasBootCode)
            LOG(2, LOG_LINE_NORMAL, L"Volume considered non-bootable, but boot code is present");
        Volume->HasBootCode = FALSE;
    }

    Volume->RootDir = LibOpenRoot(Volume->DeviceHandle);

    SetFilesystemName(Volume);
    Volume->VolName = GetVolumeName(Volume);

    if (Volume->RootDir == NULL) {
        Volume->IsReadable = FALSE;
        return;
    } else {
        Volume->IsReadable = TRUE;
        if ((GlobalConfig.LegacyType == LEGACY_TYPE_MAC) &&
            (Volume->FSType == FS_TYPE_NTFS) &&
            Volume->HasBootCode) {

                Volume->HasBootCode = HasWindowsBiosBootFiles(Volume);
        }
    }  

}  

/**
 * Walk the chain of extended boot records. A logical partition's first sector is
 * relative to the current record, while a link to the next record is relative to
 * the first extended partition. Each fabricated volume retains the whole-disk
 * Block I/O interface and stores its sector offset explicitly.
 */
static VOID ScanExtendedPartition(REFIT_VOLUME *WholeDiskVolume, MBR_PARTITION_INFO *MbrEntry)
{
    EFI_STATUS              Status;
    REFIT_VOLUME            *Volume;
    UINT32                  ExtBase, ExtCurrent, NextExtCurrent;
    UINTN                   i;
    UINTN                   LogicalPartitionIndex = 4;
    UINT8                   SectorBuffer[512];
    BOOLEAN                 Bootable;
    MBR_PARTITION_INFO      *EMbrTable;

    ExtBase = MbrEntry->StartLBA;

    for (ExtCurrent = ExtBase; ExtCurrent; ExtCurrent = NextExtCurrent) {

      Status = refit_call5_wrapper(WholeDiskVolume->BlockIO->ReadBlocks,
                                   WholeDiskVolume->BlockIO,
                                   WholeDiskVolume->BlockIO->Media->MediaId,
                                   ExtCurrent, 512, SectorBuffer);
        if (EFI_ERROR(Status)) {
            LOG(1, LOG_LINE_NORMAL, L"Error %d reading blocks from disk", Status);
            break;
        }
        if (*((UINT16 *)(SectorBuffer + 510)) != 0xaa55)
            break;
        EMbrTable = (MBR_PARTITION_INFO *)(SectorBuffer + 446);

        NextExtCurrent = 0;
        for (i = 0; i < 4; i++) {
            if ((EMbrTable[i].Flags != 0x00 && EMbrTable[i].Flags != 0x80) ||
                EMbrTable[i].StartLBA == 0 || EMbrTable[i].Size == 0)
                break;
            if (IS_EXTENDED_PART_TYPE(EMbrTable[i].Type)) {

                NextExtCurrent = ExtBase + EMbrTable[i].StartLBA;
                break;
            } else {

                Volume = AllocateZeroPool(sizeof(REFIT_VOLUME));
                Volume->DiskKind = WholeDiskVolume->DiskKind;
                Volume->IsMbrPartition = TRUE;
                Volume->MbrPartitionIndex = LogicalPartitionIndex++;
                Volume->VolName = PoolPrint(L"Partition %d", Volume->MbrPartitionIndex + 1);
                Volume->BlockIO = WholeDiskVolume->BlockIO;
                Volume->BlockIOOffset = ExtCurrent + EMbrTable[i].StartLBA;
                Volume->WholeDiskBlockIO = WholeDiskVolume->BlockIO;

                Bootable = FALSE;
                ScanVolumeBootcode(Volume, &Bootable);
                if (!Bootable)
                    Volume->HasBootCode = FALSE;
                SetVolumeBadgeIcon(Volume);
                AddListElement((VOID ***) &Volumes, &VolumesCount, Volume);
            }  
        }  
    }  
}  

/**
 * Rebuild the array from firmware Block I/O handles. A repeated nonzero
 * filesystem UUID marks later instances unreadable while retaining their volume
 * records. The second pass associates partitions with whole disks and appends
 * logical partitions found through extended MBR chains.
 *
 * The MBR association compares sector count, the first 512 bytes and a minimum
 * byte sum. Equal lengths alone cannot distinguish partitions of the same size.
 */
VOID ScanVolumes(VOID)
{
    EFI_STATUS              Status;
    EFI_HANDLE              *Handles;
    REFIT_VOLUME            *Volume, *WholeDiskVolume;
    MBR_PARTITION_INFO      *MbrTable;
    UINTN                   HandleCount = 0;
    UINTN                   HandleIndex;
    UINTN                   VolumeIndex, VolumeIndex2;
    UINTN                   PartitionIndex;
    UINTN                   SectorSum, i;
    UINT8                   *SectorBuffer1, *SectorBuffer2;
    EFI_GUID                *UuidList;
    EFI_GUID                NullUuid = NULL_GUID_VALUE;

    LOG(1, LOG_LINE_SEPARATOR, L"Scanning for volumes");
    MyFreePool(Volumes);
    Volumes = NULL;
    VolumesCount = 0;
    ForgetPartitionTables();

    Status = LibLocateHandle(ByProtocol, &BlockIoProtocol, NULL, &HandleCount, &Handles);
    LOG(2, LOG_LINE_NORMAL, L"Found handles for %d volumes", HandleCount);
    if (Status == EFI_NOT_FOUND) {
        return;   
    }
    if (CheckError(Status, L"while listing all file systems"))
        return;
    UuidList = AllocateZeroPool(sizeof(EFI_GUID) * HandleCount);

    for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {
        Volume = AllocateZeroPool(sizeof(REFIT_VOLUME));
        Volume->DeviceHandle = Handles[HandleIndex];
        AddPartitionTable(Volume);
        ScanVolume(Volume);
        LOG(1, LOG_LINE_NORMAL, L"Identified volume '%s', of type%s", Volume->VolName, FSTypeName(Volume->FSType));
        if (UuidList) {
           UuidList[HandleIndex] = Volume->VolUuid;
           for (i = 0; i < HandleIndex; i++) {
              if ((CompareMem(&(Volume->VolUuid), &(UuidList[i]), sizeof(EFI_GUID)) == 0) &&
                  (CompareMem(&(Volume->VolUuid), &NullUuid, sizeof(EFI_GUID)) != 0)) {  
                 Volume->IsReadable = FALSE;
              }  
           }  
        }  

        AddListElement((VOID ***) &Volumes, &VolumesCount, Volume);

        if (Volume->DeviceHandle == SelfLoadedImage->DeviceHandle)
            SelfVolume = Volume;
    }
    MyFreePool(UuidList);
    MyFreePool(Handles);

    if (SelfVolume == NULL)
        Print(L"WARNING: SelfVolume not found");

    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
        Volume = Volumes[VolumeIndex];

        if (Volume->BlockIO != NULL && Volume->WholeDiskBlockIO != NULL &&
            Volume->BlockIO == Volume->WholeDiskBlockIO && Volume->BlockIOOffset == 0 &&
            Volume->MbrPartitionTable != NULL) {
            MbrTable = Volume->MbrPartitionTable;
            for (PartitionIndex = 0; PartitionIndex < 4; PartitionIndex++) {
                if (IS_EXTENDED_PART_TYPE(MbrTable[PartitionIndex].Type)) {
                   ScanExtendedPartition(Volume, MbrTable + PartitionIndex);
                }
            }
        }

        WholeDiskVolume = NULL;
        if (Volume->BlockIO != NULL && Volume->WholeDiskBlockIO != NULL &&
            Volume->BlockIO != Volume->WholeDiskBlockIO) {
            for (VolumeIndex2 = 0; VolumeIndex2 < VolumesCount; VolumeIndex2++) {
                if (Volumes[VolumeIndex2]->BlockIO == Volume->WholeDiskBlockIO &&
                    Volumes[VolumeIndex2]->BlockIOOffset == 0) {
                    WholeDiskVolume = Volumes[VolumeIndex2];
                }
            }
        }

        if (WholeDiskVolume != NULL && WholeDiskVolume->MbrPartitionTable != NULL) {

            MbrTable = WholeDiskVolume->MbrPartitionTable;
            SectorBuffer1 = AllocatePool(512);
            SectorBuffer2 = AllocatePool(512);
            for (PartitionIndex = 0; PartitionIndex < 4; PartitionIndex++) {

                if ((UINT64)(MbrTable[PartitionIndex].Size) != Volume->BlockIO->Media->LastBlock + 1)
                    continue;

                Status = refit_call5_wrapper(Volume->BlockIO->ReadBlocks,
                                             Volume->BlockIO, Volume->BlockIO->Media->MediaId,
                                             Volume->BlockIOOffset, 512, SectorBuffer1);
                if (EFI_ERROR(Status))
                    break;
                Status = refit_call5_wrapper(Volume->WholeDiskBlockIO->ReadBlocks,
                                             Volume->WholeDiskBlockIO, Volume->WholeDiskBlockIO->Media->MediaId,
                                             MbrTable[PartitionIndex].StartLBA, 512, SectorBuffer2);
                if (EFI_ERROR(Status))
                    break;
                if (CompareMem(SectorBuffer1, SectorBuffer2, 512) != 0)
                    continue;
                SectorSum = 0;
                for (i = 0; i < 512; i++)
                    SectorSum += SectorBuffer1[i];
                if (SectorSum < 1000)
                    continue;

                Volume->IsMbrPartition = TRUE;
                Volume->MbrPartitionIndex = PartitionIndex;
                if (Volume->VolName == NULL) {
                    Volume->VolName = PoolPrint(L"Partition %d", PartitionIndex + 1);
                }
                break;
            }

            MyFreePool(SectorBuffer1);
            MyFreePool(SectorBuffer2);
        }
    }  
    LOG(1, LOG_LINE_NORMAL, L"Identified %d volumes", VolumesCount);
}  

VOID SetVolumeIcons(VOID) {
    UINTN        VolumeIndex;
    REFIT_VOLUME *Volume;

    LOG(1, LOG_LINE_NORMAL, L"Setting volume icons....");
    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
        Volume = Volumes[VolumeIndex];

        LOG(2, LOG_LINE_NORMAL, L"Setting volume badge icon for volume %d", VolumeIndex);
        SetVolumeBadgeIcon(Volume);
        if (Volumes[VolumeIndex]->DiskKind == DISK_KIND_INTERNAL) {

            if (!Volume->VolIconImage) {
                LOG(2, LOG_LINE_NORMAL, L"Trying to load custom volume icon image for volume %d", VolumeIndex);
                Volume->VolIconImage = egLoadIconAnyType(Volume->RootDir, L"", L".VolumeIcon", GlobalConfig.IconSizes[ICON_SIZE_BIG]);
            }
        }
    }  
}  

/*
 * A successful existence test is only filesystem evidence; it does not
 * validate an EFI header, signature, kernel command line or boot outcome.
 */
BOOLEAN FileExists(IN EFI_FILE_PROTOCOL *BaseDir, IN CHAR16 *RelativePath)
{
    EFI_STATUS         Status;
    EFI_FILE_HANDLE    TestFile;

    if (BaseDir != NULL) {
        Status = refit_call5_wrapper(BaseDir->Open, BaseDir, &TestFile, RelativePath, EFI_FILE_MODE_READ, 0);
        if (Status == EFI_SUCCESS) {
            refit_call1_wrapper(TestFile->Close, TestFile);
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * Start with 256 bytes for one EFI_FILE_INFO. On EFI_BUFFER_TOO_SMALL use the
 * reported size, doubling if the firmware did not increase it, for up to four
 * reallocations. A successful zero-byte read ends enumeration. Filter mode 1
 * keeps directories and 2 keeps non-directories.
 */
EFI_STATUS DirNextEntry(IN EFI_FILE_PROTOCOL *Directory, IN OUT EFI_FILE_INFO **DirEntry, IN UINTN FilterMode)
{
    EFI_STATUS Status;
    VOID *Buffer;
    UINTN LastBufferSize, BufferSize;
    INTN IterCount;

    for (;;) {

        if (*DirEntry != NULL) {
           MyFreePool(*DirEntry);
           *DirEntry = NULL;
        }

        /*
         * EFI_FILE_INFO includes a variable-length filename. On
         * EFI_BUFFER_TOO_SMALL retry with the reported size, doubling when
         * the driver reports no growth. Each new directory read also releases
         * the previous entry buffer.
         */
        LastBufferSize = BufferSize = 256;
        Buffer = AllocatePool(BufferSize);
        for (IterCount = 0; ; IterCount++) {
            Status = refit_call3_wrapper(Directory->Read, Directory, &BufferSize, Buffer);
            if (Status != EFI_BUFFER_TOO_SMALL || IterCount >= 4)
                break;
            if (BufferSize <= LastBufferSize) {
                LOG(1, LOG_LINE_NORMAL,
                    L"FS Driver requests bad buffer size %d (was %d), using %d instead",
                    BufferSize, LastBufferSize, LastBufferSize * 2);
                BufferSize = LastBufferSize * 2;
            } else {
                LOG(3, LOG_LINE_NORMAL, L"Reallocating buffer from %d to %d",
                    LastBufferSize, BufferSize);
            }
            Buffer = EfiReallocatePool(Buffer, LastBufferSize, BufferSize);
            LastBufferSize = BufferSize;
        }
        if (EFI_ERROR(Status)) {
            MyFreePool(Buffer);
            Buffer = NULL;
            break;
        }

        if (BufferSize == 0) {     
            MyFreePool(Buffer);
            Buffer = NULL;
            break;
        }

        *DirEntry = (EFI_FILE_INFO *)Buffer;

        if (FilterMode == 1) {    
            if (((*DirEntry)->Attribute & EFI_FILE_DIRECTORY))
                break;
        } else if (FilterMode == 2) {    
            if (((*DirEntry)->Attribute & EFI_FILE_DIRECTORY) == 0)
                break;
        } else                    
            break;

    }
    return Status;
}

/**
 * Borrow BaseDir when no child path is requested; otherwise the iterator
 * owns only a child successfully opened by firmware. CloseDirHandle records
 * this contract so teardown never closes the caller's parent handle.
 */
VOID DirIterOpen(IN EFI_FILE_PROTOCOL *BaseDir, IN CHAR16 *RelativePath OPTIONAL, OUT REFIT_DIR_ITER *DirIter)
{
    if (RelativePath == NULL) {
        DirIter->LastStatus = EFI_SUCCESS;
        DirIter->DirHandle = BaseDir;
        DirIter->CloseDirHandle = FALSE;
    } else {
        DirIter->LastStatus = refit_call5_wrapper(BaseDir->Open, BaseDir, &(DirIter->DirHandle), RelativePath, EFI_FILE_MODE_READ, 0);
        DirIter->CloseDirHandle = EFI_ERROR(DirIter->LastStatus) ? FALSE : TRUE;
    }
    DirIter->LastFileInfo = NULL;
}

#ifndef __MAKEWITH_GNUEFI
EFI_UNICODE_COLLATION_PROTOCOL *mUnicodeCollation = NULL;

static EFI_STATUS
InitializeUnicodeCollationProtocol (VOID)
{
    EFI_STATUS  Status;

    if (mUnicodeCollation != NULL) {
        return EFI_SUCCESS;
    }

    Status = gBS->LocateProtocol(&gEfiUnicodeCollation2ProtocolGuid,
                                 NULL, (VOID **) &mUnicodeCollation);
    if (EFI_ERROR(Status)) {
        Status = gBS->LocateProtocol(&gEfiUnicodeCollationProtocolGuid,
                                     NULL, (VOID **) &mUnicodeCollation);

    }
    return Status;
}

static BOOLEAN
MetaiMatch (IN CHAR16 *String, IN CHAR16 *Pattern)
{
    if (!mUnicodeCollation) {
        InitializeUnicodeCollationProtocol();
    }
    if (mUnicodeCollation)
        return mUnicodeCollation->MetaiMatch (mUnicodeCollation, String, Pattern);
    return FALSE;  
}

#endif

/**
 * Return an allocated EFI_FILE_INFO through DirEntry. A new call clears the
 * iterator's previous pointer. Callers retaining a name must copy it or keep
 * ownership of its record. Directories bypass filename wildcard matching after
 * DirNextEntry applies the type filter.
 */
BOOLEAN DirIterNext(IN OUT REFIT_DIR_ITER *DirIter, IN UINTN FilterMode, IN CHAR16 *FilePattern OPTIONAL,
                    OUT EFI_FILE_INFO **DirEntry)
{
    BOOLEAN KeepGoing = TRUE;
    UINTN   i;
    CHAR16  *OnePattern;

    if (DirIter->LastFileInfo != NULL) {

        DirIter->LastFileInfo = NULL;
    }

    if (EFI_ERROR(DirIter->LastStatus))
        return FALSE;    

    do {
        DirIter->LastStatus = DirNextEntry(DirIter->DirHandle, &(DirIter->LastFileInfo), FilterMode);
        if (EFI_ERROR(DirIter->LastStatus))
           return FALSE;
        if (DirIter->LastFileInfo == NULL)   
            return FALSE;
        if (FilePattern != NULL) {
            if ((DirIter->LastFileInfo->Attribute & EFI_FILE_DIRECTORY))
                KeepGoing = FALSE;
            i = 0;
            while (KeepGoing && (OnePattern = FindCommaDelimited(FilePattern, i++)) != NULL) {
               if (MetaiMatch(DirIter->LastFileInfo->FileName, OnePattern))
                   KeepGoing = FALSE;
               MyFreePool(OnePattern);
            }  

        } else
            break;
   } while (KeepGoing && FilePattern);

    *DirEntry = DirIter->LastFileInfo;
    return TRUE;
}

/**
 * Release the retained EFI_FILE_INFO and close only an owned child
 * directory. The result represents the last enumeration status, not
 * the result of the optional firmware Close call.
 */
EFI_STATUS DirIterClose(IN OUT REFIT_DIR_ITER *DirIter)
{
    if (DirIter->LastFileInfo != NULL) {
        MyFreePool(DirIter->LastFileInfo);
        DirIter->LastFileInfo = NULL;
    }
    if ((DirIter->CloseDirHandle) && (DirIter->DirHandle->Close))
        refit_call1_wrapper(DirIter->DirHandle->Close, DirIter->DirHandle);
    return DirIter->LastStatus;
}

/**
 * Duplicate the last UTF-16 path component after either slash separator.
 * The result is owned by the caller and does not alias its input; a trailing
 * separator represents an empty final component.
 */
CHAR16 * Basename(IN CHAR16 *Path)
{
    CHAR16  *FileName;
    UINTN   i;

    FileName = Path;

    if (Path != NULL) {
        for (i = StrLen(Path); i > 0; i--) {
            if (Path[i-1] == '\\' || Path[i-1] == '/') {
                FileName = Path + i;
                break;
            }
        }
    }

    return StrDuplicate(FileName);
}

CHAR16 * StripEfiExtension(IN CHAR16 *FileName) {
    UINTN  Length;
    CHAR16 *Copy = NULL;

    if ((FileName != NULL) && ((Copy = StrDuplicate(FileName)) != NULL)) {
        Length = StrLen(Copy);
        if ((Length >= 4) && MyStriCmp(&Copy[Length - 4], L".efi")) {
            Copy[Length - 4] = 0;
        }  
    }  
    return Copy;
}  

/**
 * Search a bounded sector sample, including the final valid start offset.
 * Reject an oversized search term before subtracting unsigned lengths,
 * otherwise malformed input may trigger an out-of-bounds memory scan.
 * An empty term matches at zero; NULL nonempty inputs cannot match.
 */
INTN FindMem(IN VOID *Buffer, IN UINTN BufferLength, IN VOID *SearchString, IN UINTN SearchStringLength)
{
    UINT8 *BufferPtr;
    UINTN Offset;

    if (SearchStringLength == 0)
        return 0;
    if (Buffer == NULL || SearchString == NULL || SearchStringLength > BufferLength)
        return -1;

    BufferPtr = Buffer;
    for (Offset = 0; Offset <= BufferLength - SearchStringLength; Offset++, BufferPtr++) {
        if (CompareMem(BufferPtr, SearchString, SearchStringLength) == 0)
            return (INTN)Offset;
    }
    return -1;
}

CHAR16 *FindExtension(IN CHAR16 *Path) {
    CHAR16     *Extension;
    BOOLEAN    Found = FALSE, FoundSlash = FALSE;
    INTN       i;

    Extension = AllocateZeroPool(sizeof(CHAR16));
    if (Path) {
        i = StrLen(Path);
        while ((!Found) && (!FoundSlash) && (i >= 0)) {
            if (Path[i] == L'.')
                Found = TRUE;
            else if ((Path[i] == L'/') || (Path[i] == L'\\'))
                FoundSlash = TRUE;
            if (!Found)
                i--;
        }  
        if (Found) {
            MergeStrings(&Extension, &Path[i], 0);
            ToLower(Extension);
        }  
    }  
    return (Extension);
}  

CHAR16 *FindLastDirName(IN CHAR16 *Path) {
    UINTN i, StartOfElement = 0, EndOfElement = 0, PathLength, CopyLength;
    CHAR16 *Found = NULL;

    if (Path == NULL)
        return NULL;

    PathLength = StrLen(Path);

    for (i = 0; i < PathLength; i++) {
        if (Path[i] == '\\') {
            StartOfElement = EndOfElement;
            EndOfElement = i;
        }  
    }  

    if (EndOfElement > 0) {
        while ((StartOfElement < PathLength) && (Path[StartOfElement] == '\\')) {
            StartOfElement++;
        }  
        EndOfElement--;
        if (EndOfElement >= StartOfElement) {
            CopyLength = EndOfElement - StartOfElement + 1;
            Found = StrDuplicate(&Path[StartOfElement]);
            if (Found != NULL)
                Found[CopyLength] = 0;
        }  
    }  
    return (Found);
}  

CHAR16 *FindPath(IN CHAR16* FullPath) {
    UINTN i, LastBackslash = 0;
    CHAR16 *PathOnly = NULL;

    if (FullPath != NULL) {
        for (i = 0; i < StrLen(FullPath); i++) {
            if (FullPath[i] == '\\')
                LastBackslash = i;
        }  
        PathOnly = StrDuplicate(FullPath);
        if (PathOnly != NULL)
            PathOnly[LastBackslash] = 0;
    }  
    return (PathOnly);
}

/*
 * Convert a firmware device path into a candidate volume and loader
 * filename. The function first releases the caller's existing output
 * pointers, then splits the printable path; callers must pass
 * appropriately owned prior values and must not supply borrowed pointers
 * to the global volume inventory. A textual device-path match is not
 * firmware image authentication.
 */
VOID FindVolumeAndFilename(IN EFI_DEVICE_PATH *loadpath,
                           OUT REFIT_VOLUME **DeviceVolume,
                           OUT CHAR16 **loader) {
    CHAR16 *DeviceString, *VolumeDeviceString, *Temp;
    UINTN i = 0;
    BOOLEAN Found = FALSE;

    if (!loadpath || !DeviceVolume || !loader)
        return;

    MyFreePool(*loader);
    MyFreePool(*DeviceVolume);
    *DeviceVolume = NULL;
    DeviceString = DevicePathToStr(loadpath);
    *loader = SplitDeviceString(DeviceString);

    while ((i < VolumesCount) && (!Found)) {
        if (Volumes[i]->DevicePath == NULL) {
            i++;
            continue;
        }
        VolumeDeviceString = DevicePathToStr(Volumes[i]->DevicePath);
        Temp = SplitDeviceString(VolumeDeviceString);
        if (MyStrStr(VolumeDeviceString, DeviceString)) {
            Found = TRUE;
            *DeviceVolume = Volumes[i];
        }
        MyFreePool(Temp);
        MyFreePool(VolumeDeviceString);
        i++;
    }  

    MyFreePool(DeviceString);
}  

BOOLEAN SplitVolumeAndFilename(IN OUT CHAR16 **Path, OUT CHAR16 **VolName) {
    UINTN i = 0, Length;
    CHAR16 *Filename;

    if (*Path == NULL)
        return FALSE;

    if (*VolName != NULL) {
        MyFreePool(*VolName);
        *VolName = NULL;
    }

    Length = StrLen(*Path);
    while ((i < Length) && ((*Path)[i] != L':')) {
        i++;
    }  

    if (i < Length) {
        Filename = StrDuplicate((*Path) + i + 1);
        (*Path)[i] = 0;
        *VolName = *Path;
        *Path = Filename;
        return TRUE;
    } else {
        return FALSE;
    }
}  

VOID SplitPathName(CHAR16 *InPath, CHAR16 **VolName, CHAR16 **Path, CHAR16 **Filename) {
    CHAR16 *Temp = NULL;

    MyFreePool(*VolName);
    MyFreePool(*Path);
    MyFreePool(*Filename);
    *VolName = *Path = *Filename = NULL;
    Temp = StrDuplicate(InPath);
    SplitVolumeAndFilename(&Temp, VolName);  
    CleanUpPathNameSlashes(Temp);
    *Path = FindPath(Temp);  
    *Filename = StrDuplicate(Temp + StrLen(*Path));
    CleanUpPathNameSlashes(*Filename);
    if (StrLen(*Path) == 0) {
        MyFreePool(*Path);
        *Path = NULL;
    }
    if (StrLen(*Filename) == 0) {
        MyFreePool(*Filename);
        *Filename = NULL;
    }
    MyFreePool(Temp);
}  

BOOLEAN FindVolume(REFIT_VOLUME **Volume, CHAR16 *Identifier) {
    UINTN     i = 0;
    BOOLEAN   Found = FALSE;

    while ((i < VolumesCount) && (!Found)) {
        if (VolumeMatchesDescription(Volumes[i], Identifier)) {
            *Volume = Volumes[i];
            Found = TRUE;
        }  
        i++;
    }  
    return (Found);
}  

BOOLEAN VolumeMatchesDescription(REFIT_VOLUME *Volume, CHAR16 *Description) {
    EFI_GUID TargetVolGuid = NULL_GUID_VALUE;

    if ((Volume == NULL) || (Description == NULL))
        return FALSE;
    if (IsGuid(Description)) {
        TargetVolGuid = StringAsGuid(Description);
        return GuidsAreEqual(&TargetVolGuid, &(Volume->PartGuid));
    } else {
        return (MyStriCmp(Description, Volume->VolName) ||
                MyStriCmp(Description, Volume->PartName) ||
                MyStriCmp(Description, Volume->FsName));
    }
}  

BOOLEAN FilenameIn(REFIT_VOLUME *Volume, CHAR16 *Directory, CHAR16 *Filename, CHAR16 *List) {
    UINTN     i = 0;
    BOOLEAN   Found = FALSE;
    CHAR16    *OneElement;
    CHAR16    *TargetVolName = NULL, *TargetPath = NULL, *TargetFilename = NULL;

    if (Filename && List) {
        while (!Found && (OneElement = FindCommaDelimited(List, i++))) {
            Found = TRUE;
            SplitPathName(OneElement, &TargetVolName, &TargetPath, &TargetFilename);
            if (((TargetVolName != NULL) && (!VolumeMatchesDescription(Volume, TargetVolName))) ||
                 ((TargetPath != NULL) && (!MyStriCmp(TargetPath, Directory))) ||
                 ((TargetFilename != NULL) && (!MyStriCmp(TargetFilename, Filename)))) {
                Found = FALSE;
            }  
            MyFreePool(OneElement);
        }  
        MyFreePool(TargetVolName);
        MyFreePool(TargetPath);
        MyFreePool(TargetFilename);
    }  

    return Found;
}  

VOID MyFreePool(IN VOID *Pointer) {
    if (Pointer != NULL)
        FreePool(Pointer);
}

static EFI_GUID AppleRemovableMediaGuid = APPLE_REMOVABLE_MEDIA_PROTOCOL_GUID;

BOOLEAN EjectMedia(VOID) {
    EFI_STATUS                      Status;
    UINTN                           HandleIndex, HandleCount = 0, Ejected = 0;
    EFI_HANDLE                      *Handles, Handle;
    APPLE_REMOVABLE_MEDIA_PROTOCOL  *Ejectable;

    Status = LibLocateHandle(ByProtocol, &AppleRemovableMediaGuid, NULL, &HandleCount, &Handles);
    if (EFI_ERROR(Status) || HandleCount == 0)
        return (FALSE);  

    for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {
        Handle = Handles[HandleIndex];
        Status = refit_call3_wrapper(BS->HandleProtocol, Handle, &AppleRemovableMediaGuid, (VOID **) &Ejectable);
        if (EFI_ERROR(Status))
            continue;
        Status = refit_call1_wrapper(Ejectable->Eject, Ejectable);
        if (!EFI_ERROR(Status))
            Ejected++;
    }
    MyFreePool(Handles);
    return (Ejected > 0);
}  

BOOLEAN GuidsAreEqual(EFI_GUID *Guid1, EFI_GUID *Guid2) {
    return (CompareMem(Guid1, Guid2, 16) == 0);
}  

VOID EraseUint32List(UINT32_LIST **TheList) {
    UINT32_LIST *NextItem;

    while (*TheList) {
        NextItem = (*TheList)->Next;
        MyFreePool(*TheList);
        *TheList = NextItem;
    }  
}  
