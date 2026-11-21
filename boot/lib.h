/*
 * Volume, EFI variable, filesystem iteration and device-path contracts.
 * RootDir implies a usable firmware filesystem protocol; detected filesystem
 * type alone does not. Methods returning allocated CHAR16 or device paths
 * must be paired with their stated release mechanism in the implementation.
 */

#ifndef __LIB_H_
#define __LIB_H_

#ifdef __MAKEWITH_GNUEFI
#include "efi.h"
#include "efilib.h"
#define EFI_DEVICE_PATH_PROTOCOL EFI_DEVICE_PATH
#else
#include "../include/tiano_includes.h"
#endif

#include "global.h"

#include "libeg.h"

/*
 * The iterator can borrow its caller's directory or open a child directory
 * itself. CloseDirHandle records that distinction. LastFileInfo is managed by
 * the iterator and can be replaced by the next read.
 */
typedef struct {
    EFI_STATUS          LastStatus;
    EFI_FILE_HANDLE     DirHandle;
    BOOLEAN             CloseDirHandle;
    EFI_FILE_INFO       *LastFileInfo;
} REFIT_DIR_ITER;

#define DISK_KIND_INTERNAL  (0)
#define DISK_KIND_EXTERNAL  (1)
#define DISK_KIND_OPTICAL   (2)
#define DISK_KIND_NET       (3)

#define VOL_UNREADABLE 999

#define IS_EXTENDED_PART_TYPE(type) ((type) == 0x05 || (type) == 0x0f || (type) == 0x85)

#define GPT_READ_ONLY     0x1000000000000000
#define GPT_NO_AUTOMOUNT  0x8000000000000000

#define IGNORE_PARTITION_NAMES L"Microsoft basic data,Linux filesystem,Apple HFS/HFS+"

extern EFI_GUID gFreedesktopRootGuid;

EFI_STATUS InitRefitLib(IN EFI_HANDLE ImageHandle);
VOID UninitRefitLib(VOID);
EFI_STATUS ReinitRefitLib(VOID);

EFI_STATUS EfivarGetRaw(IN EFI_GUID *vendor, IN CHAR16 *name, OUT CHAR8 **buffer, OUT UINTN *size OPTIONAL);
EFI_STATUS EfivarSetRaw(EFI_GUID *vendor, CHAR16 *name, CHAR8 *buf, UINTN size, BOOLEAN persistent);

VOID CleanUpPathNameSlashes(IN OUT CHAR16 *PathName);
CHAR16* SplitDeviceString(IN OUT CHAR16 *InString);
VOID CreateList(OUT VOID ***ListPtr, OUT UINTN *ElementCount, IN UINTN InitialElementCount);
VOID AddListElement(IN OUT VOID ***ListPtr, IN OUT UINTN *ElementCount, IN VOID *NewElement);
VOID FreeList(IN OUT VOID ***ListPtr, IN OUT UINTN *ElementCount);

VOID SetVolumeBadgeIcon(REFIT_VOLUME *Volume);
VOID ScanVolumes(VOID);
VOID SetVolumeIcons(VOID);

BOOLEAN FileExists(IN EFI_FILE_PROTOCOL *BaseDir, IN CHAR16 *RelativePath);

EFI_STATUS DirNextEntry(IN EFI_FILE_PROTOCOL *Directory, IN OUT EFI_FILE_INFO **DirEntry, IN UINTN FilterMode);

VOID DirIterOpen(IN EFI_FILE_PROTOCOL *BaseDir, IN CHAR16 *RelativePath OPTIONAL, OUT REFIT_DIR_ITER *DirIter);
BOOLEAN DirIterNext(IN OUT REFIT_DIR_ITER *DirIter, IN UINTN FilterMode, IN CHAR16 *FilePattern OPTIONAL, OUT EFI_FILE_INFO **DirEntry);
EFI_STATUS DirIterClose(IN OUT REFIT_DIR_ITER *DirIter);

CHAR16 * Basename(IN CHAR16 *Path);
CHAR16 * StripEfiExtension(CHAR16 *FileName);

INTN FindMem(IN VOID *Buffer, IN UINTN BufferLength, IN VOID *SearchString, IN UINTN SearchStringLength);
VOID ReinitVolumes(VOID);

CHAR16 *FindExtension(IN CHAR16 *Path);
CHAR16 *FindLastDirName(IN CHAR16 *Path);
CHAR16 *FindPath(IN CHAR16* FullPath);
VOID FindVolumeAndFilename(IN EFI_DEVICE_PATH *loadpath, OUT REFIT_VOLUME **DeviceVolume, OUT CHAR16 **loader);
BOOLEAN SplitVolumeAndFilename(IN OUT CHAR16 **Path, OUT CHAR16 **VolName);
VOID SplitPathName(CHAR16 *InPath, CHAR16 **VolName, CHAR16 **Path, CHAR16 **Filename);
BOOLEAN FindVolume(REFIT_VOLUME **Volume, CHAR16 *Identifier);
BOOLEAN VolumeMatchesDescription(REFIT_VOLUME *Volume, CHAR16 *Description);
BOOLEAN FilenameIn(IN REFIT_VOLUME *Volume, IN CHAR16 *Directory, IN CHAR16 *Filename, IN CHAR16 *List);
VOID MyFreePool(IN OUT VOID *Pointer);

#ifdef __MAKEWITH_GNUEFI
#define MyCopyMem(Dest, Src, len) refit_call3_wrapper(gBS->CopyMem, Dest, Src, len)
#else
#define MyCopyMem(Dest, Src, len) CopyMem(Dest, Src, len)
#endif

BOOLEAN EjectMedia(VOID);

BOOLEAN GuidsAreEqual(EFI_GUID *Guid1, EFI_GUID *Guid2);

VOID EraseUint32List(UINT32_LIST **TheList);

#endif
