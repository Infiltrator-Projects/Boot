/*
 * BOOT / Firmware service API
 *
 * Firmware volume, file, variable and device-path interfaces.
 *
 * RootDir identifies a usable EFI file protocol, not merely a disk type.
 * Allocated strings and device paths require paired release.
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

/*
 * Init borrows the entry image handle and opens firmware-facing volume state.
 * Uninit closes active roots; Reinit restores them if a launched image returns.
 */
EFI_STATUS InitRefitLib(IN EFI_HANDLE ImageHandle);
VOID UninitRefitLib(VOID);
EFI_STATUS ReinitRefitLib(VOID);

/*
 * Success returns a caller-owned EFI pool buffer. The size argument is
 * genuinely optional and output is zeroed on failure when provided.
 * GlobalGuid variables including BootOrder use firmware NVRAM; BootGuid
 * can be configured to use file-backed storage instead.
 * SetRaw may change persistent state; sizes count bytes, not CHAR16 units.
 */
EFI_STATUS EfivarGetRaw(IN EFI_GUID *vendor, IN CHAR16 *name, OUT CHAR8 **buffer, OUT UINTN *size OPTIONAL);
EFI_STATUS EfivarSetRaw(EFI_GUID *vendor, CHAR16 *name, CHAR8 *buf, UINTN size, BOOLEAN persistent);

VOID CleanUpPathNameSlashes(IN OUT CHAR16 *PathName);
CHAR16* SplitDeviceString(IN OUT CHAR16 *InString);
/*
 * List storage is reallocated as a pointer vector. AddListElement cannot
 * report allocation failure (VOID return), so a failed append leaves the
 * old count unchanged. FreeList releases individually owned elements and
 * does not reset the caller's pointer or count.
 */
VOID CreateList(OUT VOID ***ListPtr, OUT UINTN *ElementCount, IN UINTN InitialElementCount);
VOID AddListElement(IN OUT VOID ***ListPtr, IN OUT UINTN *ElementCount, IN VOID *NewElement);
VOID FreeList(IN OUT VOID ***ListPtr, IN OUT UINTN *ElementCount);

VOID SetVolumeBadgeIcon(REFIT_VOLUME *Volume);
/*
 * Rebuild the global Volumes inventory from firmware Block I/O handles and
 * partition metadata. Discovery can produce an unreadable volume record;
 * callers must not equate a recognised filesystem with an open RootDir.
 * Selections referencing a previous inventory require reconsideration.
 */
VOID ScanVolumes(VOID);
VOID SetVolumeIcons(VOID);

BOOLEAN FileExists(IN EFI_FILE_PROTOCOL *BaseDir, IN CHAR16 *RelativePath);

/*
 * Advance the supplied firmware directory cursor. FilterMode 1 accepts
 * directories, 2 non-directories, otherwise either. Each call releases the
 * preceding *DirEntry allocation; a successful end-of-directory read may
 * leave *DirEntry NULL. Do not retain that buffer across another call.
 */
EFI_STATUS DirNextEntry(IN EFI_FILE_PROTOCOL *Directory, IN OUT EFI_FILE_INFO **DirEntry, IN UINTN FilterMode);

/*
 * An iterator borrows BaseDir unless it opens a child. A returned DirEntry
 * aliases LastFileInfo: callers freeing that record must advance the iterator
 * or clear the field before closing it to prevent a second free. DirIterClose
 * returns the prior enumeration status, not the firmware Close status.
 */
VOID DirIterOpen(IN EFI_FILE_PROTOCOL *BaseDir, IN CHAR16 *RelativePath OPTIONAL, OUT REFIT_DIR_ITER *DirIter);
BOOLEAN DirIterNext(IN OUT REFIT_DIR_ITER *DirIter, IN UINTN FilterMode, IN CHAR16 *FilePattern OPTIONAL, OUT EFI_FILE_INFO **DirEntry);
EFI_STATUS DirIterClose(IN OUT REFIT_DIR_ITER *DirIter);

/* Caller owns the independent UTF-16 string returned by Basename. */
CHAR16 * Basename(IN CHAR16 *Path);
/* Duplicate FileName and strip a trailing .efi suffix when present; caller frees. */
CHAR16 * StripEfiExtension(CHAR16 *FileName);

INTN FindMem(IN VOID *Buffer, IN UINTN BufferLength, IN VOID *SearchString, IN UINTN SearchStringLength);
VOID ReinitVolumes(VOID);

/*
 * These three helpers return independently allocated UTF-16 strings: a
 * lower-case extension, the last directory component, and the directory
 * prefix respectively. They do not inspect filesystem contents. NULL may
 * indicate absent input or allocation failure depending on the helper.
 */
CHAR16 *FindExtension(IN CHAR16 *Path);
CHAR16 *FindLastDirName(IN CHAR16 *Path);
CHAR16 *FindPath(IN CHAR16* FullPath);
/*
 * WARNING: the implementation frees previous *loader and *DeviceVolume
 * values before assigning outputs. Only pass previously owned allocations
 * to those slots; a borrowed pointer from Volumes must never be passed as
 * *DeviceVolume. Textual device-path matching is not identity validation.
 */
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

/*
 * Invoke Apple removable-media Eject on matching firmware handles. TRUE
 * means at least one invocation succeeded, not that every device was ejected.
 */
BOOLEAN EjectMedia(VOID);

/* Compare two non-NULL EFI_GUID objects by their 16-byte representation. */
BOOLEAN GuidsAreEqual(EFI_GUID *Guid1, EFI_GUID *Guid2);

/* Free the linked UINT32_LIST nodes and leave the caller's head NULL. */
VOID EraseUint32List(UINT32_LIST **TheList);

#endif
