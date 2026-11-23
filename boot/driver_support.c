/*
 * BOOT / Driver binding
 *
 * Loads and binds firmware filesystem drivers, then reconnects controllers
 * so subsequent volume scans can discover newly accessible filesystems.
 *
 * Finding a driver filename is distinct from authenticating, starting and
 * binding its image. A successful discovery count is not proof of a readable
 * mounted volume.
 */

#include "driver_support.h"
#include "lib.h"
#include "mystrings.h"
#include "screen.h"
#include "launch_efi.h"
#include "log.h"
#include "../include/boot_call_wrapper.h"
#include "lvm2.h"

/* Use exactly the same EFI Driver Binding ABI as the compiled filesystem
 * drivers. GNU-EFI keeps the interface in the filesystem bridge header. */
#ifdef __MAKEWITH_GNUEFI
#include "../filesystems/edk2/DriverBinding.h"
#else
typedef EFI_DRIVER_BINDING_PROTOCOL BOOT_EFI_DRIVER_BINDING_PROTOCOL;
#endif

#if defined (EFIX64)
#define DRIVER_DIRS             L"drivers,drivers_x64"
#elif defined (EFI32)
#define DRIVER_DIRS             L"drivers,drivers_ia32"
#elif defined (EFIAARCH64)
#define DRIVER_DIRS             L"drivers,drivers_aa64"
#else
#define DRIVER_DIRS             L"drivers"
#endif

EFI_GUID gMyEfiLoadedImageProtocolGuid = { 0x5B1B31A1, 0x9562, 0x11D2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B }};
EFI_GUID gMyEfiDriverBindingProtocolGuid = { 0x18A031AB, 0xB443, 0x4D1A, { 0xA5, 0xC0, 0x0C, 0x09, 0x26, 0x1E, 0x9F, 0x71 }};
EFI_GUID gMyEfiDriverConfigurationProtocolGuid = { 0x107A772B, 0xD5E1, 0x11D4, { 0x9A, 0x46, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D }};
EFI_GUID gMyEfiDriverDiagnosticsProtocolGuid = { 0x0784924F, 0xE296, 0x11D4, { 0x9A, 0x49, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D }};
EFI_GUID gMyEfiComponentNameProtocolGuid = { 0x107A772C, 0xD5E1, 0x11D4, { 0x9A, 0x46, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D }};
EFI_GUID gMyEfiDevicePathProtocolGuid = { 0x09576E91, 0x6D3F, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B }};
EFI_GUID gMyEfiDiskIoProtocolGuid = { 0xCE345171, 0xBA0B, 0x11D2, { 0x8E, 0x4F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B }};
EFI_GUID gMyEfiBlockIoProtocolGuid = { 0x964E5B21, 0x6459, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B }};
EFI_GUID gMyEfiSimpleFileSystemProtocolGuid = { 0x964E5B22, 0x6459, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B }};

#ifdef __MAKEWITH_GNUEFI
struct MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
struct MY_EFI_FILE_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(
  IN struct MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL    *This,
  OUT struct MY_EFI_FILE_PROTOCOL                 **Root
  );

typedef struct _MY_MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
  UINT64                                      Revision;
  MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
} MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct _MY_EFI_FILE_PROTOCOL {
  UINT64                Revision;
  EFI_FILE_OPEN         Open;
  EFI_FILE_CLOSE        Close;
  EFI_FILE_DELETE       Delete;
  EFI_FILE_READ         Read;
  EFI_FILE_WRITE        Write;
  EFI_FILE_GET_POSITION GetPosition;
  EFI_FILE_SET_POSITION SetPosition;
  EFI_FILE_GET_INFO     GetInfo;
  EFI_FILE_SET_INFO     SetInfo;
  EFI_FILE_FLUSH        Flush;
} MY_EFI_FILE_PROTOCOL;

typedef struct _MY_EFI_BLOCK_IO_PROTOCOL {
  UINT64             Revision;
  EFI_BLOCK_IO_MEDIA *Media;
  EFI_BLOCK_RESET    Reset;
  EFI_BLOCK_READ     ReadBlocks;
  EFI_BLOCK_WRITE    WriteBlocks;
  EFI_BLOCK_FLUSH    FlushBlocks;
} MY_EFI_BLOCK_IO_PROTOCOL;
#else  
#define MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
#define MY_EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL
#define MY_EFI_BLOCK_IO_PROTOCOL EFI_BLOCK_IO_PROTOCOL
#endif

EFI_STATUS
/*
 * Classifies the firmware's handle database from installed protocols and
 * OpenProtocolInformation relationships. The returned handle list and
 * classification array are caller-owned allocations. Controller topology
 * may change as drivers bind. A NULL ControllerHandle classifies parent
 * relationships for all handles in one current snapshot; a specified handle
 * retains the targeted query semantics. The caller must not reuse the snapshot
 * across independent discovery phases.
 */
LibScanHandleDatabase (EFI_HANDLE  DriverBindingHandle, OPTIONAL
                       UINT32      *DriverBindingHandleIndex, OPTIONAL
                       EFI_HANDLE  ControllerHandle, OPTIONAL
                       UINT32      *ControllerHandleIndex, OPTIONAL
                       UINTN       *HandleCount,
                       EFI_HANDLE  **HandleBuffer,
                       UINT32      **HandleType) {
  EFI_STATUS                          Status;
  UINTN                               HandleIndex;
  EFI_GUID                            **ProtocolGuidArray;
  UINTN                               ArrayCount;
  UINTN                               ProtocolIndex;
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *OpenInfo;
  UINTN                               OpenInfoCount;
  UINTN                               OpenInfoIndex;
  UINTN                               ChildIndex;
  BOOLEAN                             DriverBindingHandleIndexValid;

  DriverBindingHandleIndexValid = FALSE;
  if (DriverBindingHandleIndex != NULL) {
    *DriverBindingHandleIndex = 0xffffffff;
  }

  if (ControllerHandleIndex != NULL) {
    *ControllerHandleIndex = 0xffffffff;
  }

  *HandleCount  = 0;
  *HandleBuffer = NULL;
  *HandleType   = NULL;

  Status = boot_call5_wrapper(BS->LocateHandleBuffer,
     AllHandles,
     NULL,
     NULL,
     HandleCount,
     HandleBuffer
  );
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  *HandleType = AllocatePool (*HandleCount * sizeof (UINT32));
  if (*HandleType == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Error;
  }

  for (HandleIndex = 0; HandleIndex < *HandleCount; HandleIndex++) {

    (*HandleType)[HandleIndex] = EFI_HANDLE_TYPE_UNKNOWN;

    if (DriverBindingHandle != NULL &&
        DriverBindingHandleIndex != NULL &&
        (*HandleBuffer)[HandleIndex] == DriverBindingHandle
        ) {
      *DriverBindingHandleIndex     = (UINT32) HandleIndex;
      DriverBindingHandleIndexValid = TRUE;
    }

    if (ControllerHandle != NULL && ControllerHandleIndex != NULL && (*HandleBuffer)[HandleIndex] == ControllerHandle) {
      *ControllerHandleIndex      = (UINT32) HandleIndex;
    }

  }

  for (HandleIndex = 0; HandleIndex < *HandleCount; HandleIndex++) {

    Status = boot_call3_wrapper(BS->ProtocolsPerHandle,
                  (*HandleBuffer)[HandleIndex],
                  &ProtocolGuidArray,
                  &ArrayCount
                  );
    if (!EFI_ERROR (Status)) {

      for (ProtocolIndex = 0; ProtocolIndex < ArrayCount; ProtocolIndex++) {

        if (CompareGuid (ProtocolGuidArray[ProtocolIndex], &gMyEfiLoadedImageProtocolGuid) == 0) {
          (*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_IMAGE_HANDLE;
        }

        if (CompareGuid (ProtocolGuidArray[ProtocolIndex], &gMyEfiDriverBindingProtocolGuid) == 0) {
          (*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_DRIVER_BINDING_HANDLE;
        }

        if (CompareGuid (ProtocolGuidArray[ProtocolIndex], &gMyEfiDriverConfigurationProtocolGuid) == 0) {
          (*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_DRIVER_CONFIGURATION_HANDLE;
        }

        if (CompareGuid (ProtocolGuidArray[ProtocolIndex], &gMyEfiDriverDiagnosticsProtocolGuid) == 0) {
          (*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_DRIVER_DIAGNOSTICS_HANDLE;
        }

        if (CompareGuid (ProtocolGuidArray[ProtocolIndex], &gMyEfiComponentNameProtocolGuid) == 0) {
          (*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_COMPONENT_NAME_HANDLE;
        }

        if (CompareGuid (ProtocolGuidArray[ProtocolIndex], &gMyEfiDevicePathProtocolGuid) == 0) {
          (*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_DEVICE_HANDLE;
        }

        Status = boot_call4_wrapper(BS->OpenProtocolInformation,
                      (*HandleBuffer)[HandleIndex],
                      ProtocolGuidArray[ProtocolIndex],
                      &OpenInfo,
                      &OpenInfoCount
                      );
        if (!EFI_ERROR (Status)) {

          for (OpenInfoIndex = 0; OpenInfoIndex < OpenInfoCount; OpenInfoIndex++) {
            if (DriverBindingHandle != NULL && OpenInfo[OpenInfoIndex].AgentHandle == DriverBindingHandle) {
              if ((OpenInfo[OpenInfoIndex].Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) == EFI_OPEN_PROTOCOL_BY_DRIVER) {

                (*HandleType)[HandleIndex] |= (EFI_HANDLE_TYPE_DEVICE_HANDLE | EFI_HANDLE_TYPE_CONTROLLER_HANDLE);

                if (DriverBindingHandleIndexValid) {
                  (*HandleType)[*DriverBindingHandleIndex] |= EFI_HANDLE_TYPE_DEVICE_DRIVER;
                }
              }

              if ((
                    OpenInfo[OpenInfoIndex].Attributes & EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                ) == EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                  ) {

                if (DriverBindingHandleIndexValid) {
                  (*HandleType)[*DriverBindingHandleIndex] |= EFI_HANDLE_TYPE_BUS_DRIVER;
                }
              }

              if (ControllerHandle != NULL && (*HandleBuffer)[HandleIndex] == ControllerHandle) {
                if ((
                      OpenInfo[OpenInfoIndex].Attributes & EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                  ) == EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                    ) {
                  for (ChildIndex = 0; ChildIndex < *HandleCount; ChildIndex++) {
                    if ((*HandleBuffer)[ChildIndex] == OpenInfo[OpenInfoIndex].ControllerHandle) {
                      (*HandleType)[ChildIndex] |= (EFI_HANDLE_TYPE_DEVICE_HANDLE | EFI_HANDLE_TYPE_CHILD_HANDLE);
                    }
                  }
                }
              }
            }

            if (DriverBindingHandle == NULL &&
                (ControllerHandle == NULL || OpenInfo[OpenInfoIndex].ControllerHandle == ControllerHandle)) {
              if ((OpenInfo[OpenInfoIndex].Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) == EFI_OPEN_PROTOCOL_BY_DRIVER) {
                for (ChildIndex = 0; ChildIndex < *HandleCount; ChildIndex++) {
                  if ((*HandleBuffer)[ChildIndex] == OpenInfo[OpenInfoIndex].AgentHandle) {
                    (*HandleType)[ChildIndex] |= EFI_HANDLE_TYPE_DEVICE_DRIVER;
                  }
                }
              }

              if ((
                    OpenInfo[OpenInfoIndex].Attributes & EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                ) == EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                  ) {
                (*HandleType)[HandleIndex] |= EFI_HANDLE_TYPE_PARENT_HANDLE;
                for (ChildIndex = 0; ChildIndex < *HandleCount; ChildIndex++) {
                  if ((*HandleBuffer)[ChildIndex] == OpenInfo[OpenInfoIndex].AgentHandle) {
                    (*HandleType)[ChildIndex] |= EFI_HANDLE_TYPE_BUS_DRIVER;
                  }
                }
              }
            }
          }

          MyFreePool (OpenInfo);
        }
      }

      MyFreePool (ProtocolGuidArray);
    }
  }

  return EFI_SUCCESS;

Error:
  MyFreePool (*HandleType);
  MyFreePool (*HandleBuffer);

  *HandleCount  = 0;
  *HandleBuffer = NULL;
  *HandleType   = NULL;

  return Status;
}  

#ifdef __MAKEWITH_GNUEFI

/*
 * Filesystem drivers installed by BOOT are the only drivers permitted to
 * manage BOOT's new virtual LVM Block I/O handles. A direct Driver Binding
 * Supported/Start sequence avoids a firmware-wide ConnectController search,
 * including platform overrides and unrelated vendor/bus drivers.
 *
 * Driver image handles remain valid because BOOT does not unload successfully
 * started filesystem drivers. The list grows with loaded modules rather than
 * assuming a hardware model, partition count, filesystem type, or fixed limit.
 */
typedef struct BOOT_FS_DRIVER_NODE {
    struct BOOT_FS_DRIVER_NODE *Next;
    EFI_HANDLE Image;
    CHAR16 *Name;
} BOOT_FS_DRIVER_NODE;

static BOOT_FS_DRIVER_NODE *BootFsDrivers = NULL;
static BOOT_FS_DRIVER_NODE *BootFsDriversTail = NULL;

VOID RegisterBootFilesystemDriver(EFI_HANDLE DriverHandle, CHAR16 *DriverName)
{
    BOOT_EFI_DRIVER_BINDING_PROTOCOL *Binding = NULL;
    BOOT_FS_DRIVER_NODE *Walk, *Record;
    EFI_STATUS Status;

    if (!DriverHandle) return;
    Status = boot_call3_wrapper(BS->HandleProtocol, DriverHandle,
                                &gMyEfiDriverBindingProtocolGuid,
                                (VOID **)&Binding);
    if (EFI_ERROR(Status) || !Binding || !Binding->Supported || !Binding->Start)
        return;

    for (Walk = BootFsDrivers; Walk; Walk = Walk->Next)
        if (Walk->Image == DriverHandle) return;

    Record = AllocateZeroPool(sizeof(*Record));
    if (!Record) {
        LOG(1, LOG_LINE_NORMAL, L"BOOT: no memory for filesystem driver registry");
        return;
    }
    Record->Image = DriverHandle;
    Record->Name = DriverName ? StrDuplicate(DriverName) : NULL;
    if (BootFsDriversTail) BootFsDriversTail->Next = Record;
    else BootFsDrivers = Record;
    BootFsDriversTail = Record;
}

static EFI_STATUS BootBindVirtualFilesystem(EFI_HANDLE VolumeHandle)
{
    BOOT_FS_DRIVER_NODE *Record;
    BOOT_EFI_DRIVER_BINDING_PROTOCOL *Binding = NULL;
    MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    EFI_STATUS Status, VerifyStatus;
    UINTN DriverNumber = 0;

    if (!BootFsDrivers) {
        LOG(1, LOG_LINE_NORMAL,
            L"LVM2: no BOOT filesystem drivers registered for virtual volume");
        return EFI_NOT_FOUND;
    }

    for (Record = BootFsDrivers; Record; Record = Record->Next) {
        ++DriverNumber;
        Status = boot_call3_wrapper(BS->HandleProtocol, Record->Image,
                                    &gMyEfiDriverBindingProtocolGuid,
                                    (VOID **)&Binding);
        if (EFI_ERROR(Status) || !Binding || !Binding->Supported || !Binding->Start)
            continue;

        if (GlobalConfig.StartupDiagnostics) {
            LOG(1, LOG_LINE_NORMAL,
                L"[DIAG] LVM driver %d '%s' Supported BEGIN",
                DriverNumber, Record->Name ? Record->Name : L"(unnamed)");
            BootDiagFlush();
        }

        Status = boot_call3_wrapper(Binding->Supported, Binding, VolumeHandle, NULL);
        if (GlobalConfig.StartupDiagnostics) {
            LOG(1, LOG_LINE_NORMAL,
                L"[DIAG] LVM driver %d Supported END status=%lu",
                DriverNumber, (UINT64)Status);
            BootDiagFlush();
        }
        if (EFI_ERROR(Status))
            continue;

        if (GlobalConfig.StartupDiagnostics) {
            CHAR16 *DisplayText = PoolPrint(L"Mounting LVM volume with %s",
                            Record->Name ? Record->Name : L"filesystem driver");
            LOG(1, LOG_LINE_NORMAL, L"[DIAG] LVM driver %d Start BEGIN",
                DriverNumber);
            BootStartupProgressReport(DisplayText, BOOT_PROGRESS_ACTIVE);
            MyFreePool(DisplayText);
            BootDiagFlush();
        }

        Status = boot_call3_wrapper(Binding->Start, Binding, VolumeHandle, NULL);
        if (GlobalConfig.StartupDiagnostics) {
            LOG(1, LOG_LINE_NORMAL,
                L"[DIAG] LVM driver %d Start END status=%lu",
                DriverNumber, (UINT64)Status);
            BootDiagFlush();
        }
        if (EFI_ERROR(Status))
            continue;

        VerifyStatus = boot_call3_wrapper(BS->HandleProtocol, VolumeHandle,
                                          &gMyEfiSimpleFileSystemProtocolGuid,
                                          (VOID **)&Fs);
        if (!EFI_ERROR(VerifyStatus) && Fs)
            return EFI_SUCCESS;

        LOG(1, LOG_LINE_NORMAL,
            L"LVM2: driver reported start success without Simple File System");
    }

    return EFI_NOT_FOUND;
}

/*
 * Bind newly loaded filesystem drivers only to EFI storage handles that have
 * no Simple File System protocol yet. A firmware bus has already published
 * these Block I/O handles; a nonrecursive connection offers the filesystem
 * driver to the existing block device without walking unrelated PCI, USB,
 * networking, graphics or vendor-protocol controllers.
 *
 * A recursive ConnectController on arbitrary device-path handles can enter
 * firmware-dependent bus discovery without any EFI timeout mechanism. This
 * caused an observed firmware lockup during a physical-machine boot trial.
 * Do not replace this bounded storage pass with a topology-wide recursive
 * traversal. If a newly connected device publishes more Block I/O handles,
 * a later scan can consider them; existing readable volumes are left intact.
 *
 * The function does not know filesystem formats, host device names or disk
 * counts. LVM2 contributes virtual Block I/O handles through the same protocol.
 * An individual EFI_NOT_FOUND from ConnectController means that no new driver
 * bound that block handle; it does not invalidate other handles or volumes.
 */
EFI_STATUS ConnectAllDriversToAllControllers(VOID)
{
    EFI_STATUS Status, ConnectStatus;
    EFI_HANDLE *Handles = NULL;
    MY_EFI_BLOCK_IO_PROTOCOL *BlockIo;
    MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
    UINTN Count = 0, Index, Attempted = 0, VolumeIndex, Excluded = 0;
    UINTN Unsupported = 0;
    BOOLEAN Recognized;
    UINT64 PassBegin = BootDiagTicks(), ConnectBegin;

    Status = boot_call5_wrapper(BS->LocateHandleBuffer, ByProtocol,
                                &gMyEfiBlockIoProtocolGuid, NULL,
                                &Count, &Handles);
    if (Status == EFI_NOT_FOUND)
        return EFI_SUCCESS;
    if (EFI_ERROR(Status))
        return Status;

    if (GlobalConfig.StartupDiagnostics) {
        LOG(1, LOG_LINE_NORMAL,
            L"[DIAG] storage controller pass BEGIN blockHandles=%d", Count);
        BootDiagFlush();
    }

    for (Index = 0; Index < Count; ++Index) {
        Status = boot_call3_wrapper(BS->HandleProtocol, Handles[Index],
                                   &gMyEfiBlockIoProtocolGuid,
                                   (VOID **)&BlockIo);
        if (EFI_ERROR(Status) || !BlockIo || !BlockIo->Media ||
            !BlockIo->Media->MediaPresent)
            continue;

        /*
         * The initial ScanVolumes inventory already contains GPT partition
         * names for these handles. Apply dont_scan_volumes before invoking
         * ConnectController, since EFI drivers can otherwise hold up startup
         * for a firmware-media timeout on volumes that are explicitly excluded
         * from BOOT discovery. BOOT-created LVM handles were published later
         * and retain their normal explicit binding path.
         */
        for (VolumeIndex = 0; VolumeIndex < VolumesCount; ++VolumeIndex) {
            if (Volumes[VolumeIndex] &&
                Volumes[VolumeIndex]->DeviceHandle == Handles[Index] &&
                BootVolumeExcluded(Volumes[VolumeIndex]))
                break;
        }
        if (VolumeIndex < VolumesCount) {
            ++Excluded;
            LOG(1, LOG_LINE_NORMAL,
                L"Skipping EFI controller handle=%d/%d excluded by dont_scan_volumes",
                Index + 1, Count);
            continue;
        }

        /* Already readable volumes need no rebinding. Firmware-native
         * SimpleFileSystem support takes precedence over BOOT's classifiers. */
        Status = boot_call3_wrapper(BS->HandleProtocol, Handles[Index],
                                   &gMyEfiSimpleFileSystemProtocolGuid,
                                   (VOID **)&Fs);
        if (Status == EFI_SUCCESS)
            continue;

        /*
         * Only offer filesystem drivers to physical volumes whose initial
         * read-only superblock probe recognised a format actually supported
         * by BOOT. This avoids searching every loaded EFI driver against SFS,
         * PFS3, unknown and unrelated disk partitions. Newly published LVM
         * volumes are handled by the existing explicit virtual binding path.
         */
        if (!LvmIsPublishedHandle(Handles[Index])) {
            Recognized = FALSE;
            for (VolumeIndex = 0; VolumeIndex < VolumesCount; ++VolumeIndex) {
                if (Volumes[VolumeIndex] &&
                    Volumes[VolumeIndex]->DeviceHandle == Handles[Index]) {
                    Recognized = BootHasFilesystemReader(Volumes[VolumeIndex]->FSType);
                    break;
                }
            }
            if (!Recognized) {
                ++Unsupported;
                LOG(1, LOG_LINE_NORMAL,
                    L"Skipping EFI driver connection handle=%d/%d: no supported filesystem signature",
                    Index + 1, Count);
                continue;
            }
        }

        ++Attempted;
        if (GlobalConfig.StartupDiagnostics) {
            EFI_DEVICE_PATH *Path = DevicePathFromHandle(Handles[Index]);
            CHAR16 *Text = Path ? DevicePathToStr(Path) : NULL;
            CHAR16 *DisplayText;

            LOG(1, LOG_LINE_NORMAL,
                L"[DIAG] storage controller %d handle=%d/%d path='%s' BEGIN recursive=0",
                Attempted, Index + 1, Count, Text ? Text : L"(no device path)");
            DisplayText = PoolPrint(L"Connecting storage controller %d (handle %d/%d)",
                                    Attempted, Index + 1, Count);
            BootStartupProgressReport(DisplayText, BOOT_PROGRESS_ACTIVE);
            MyFreePool(DisplayText);
            MyFreePool(Text);
            BootDiagFlush();
        }

        ConnectBegin = BootDiagTicks();
        if (LvmIsPublishedHandle(Handles[Index])) {
            /* Never hand a BOOT-created virtual disk to firmware's generic
             * driver search: only started BOOT filesystem drivers probe it. */
            ConnectStatus = BootBindVirtualFilesystem(Handles[Index]);
        } else {
            ConnectStatus = boot_call4_wrapper(BS->ConnectController,
                                               Handles[Index], NULL, NULL, FALSE);
        }
        if (GlobalConfig.StartupDiagnostics) {
            UINT64 ConnectionMs = BootDiagElapsedMs(ConnectBegin);
            CHAR16 *DisplayText;

            LOG(1, LOG_LINE_NORMAL,
                L"[DIAG] storage controller %d handle=%d/%d END status=%lu elapsed=%lu ms",
                Attempted, Index + 1, Count,
                (UINT64)ConnectStatus, ConnectionMs);
            DisplayText = PoolPrint(L"Storage controller %d finished (%lu ms, EFI %lu)",
                                    Attempted, ConnectionMs, (UINT64)ConnectStatus);
            BootStartupProgressReport(DisplayText,
                EFI_ERROR(ConnectStatus) ? BOOT_PROGRESS_ERROR : BOOT_PROGRESS_OK);
            MyFreePool(DisplayText);
        }
    }

    if (GlobalConfig.StartupDiagnostics) {
        LOG(1, LOG_LINE_NORMAL,
            L"[DIAG] storage controller pass END attempts=%d excluded=%d unsupported=%d elapsed=%lu ms",
            Attempted, Excluded, Unsupported, BootDiagElapsedMs(PassBegin));
        BootDiagFlush();
    }
    MyFreePool(Handles);
    return EFI_SUCCESS;
}

#else
EFI_STATUS ConnectAllDriversToAllControllers(VOID) {
    BdsLibConnectAllDriversToAllControllers();
    return 0;
}
#endif

/**
 * Inspect partition handles exposing Disk I/O that currently lack Simple File
 * System. For each driver-open record, disconnect its agent and, only on success,
 * offer the newly started driver to ConnectController. Existing readable
 * partition handles are skipped. Each OpenProtocolInformation array belongs to
 * one enumeration iteration and is freed after that handle is processed.
 * This routine has no result channel: neither DisconnectController nor
 * ConnectController failure propagates to its caller. Disconnecting an existing
 * driver can affect access to the volume; a subsequent rescan is needed to
 * observe which Simple File System protocols remain available. This is a
 * controller rebinding operation, not evidence that a newly loaded filesystem
 * driver successfully mounted a volume.
 */
VOID ConnectFilesystemDriver(EFI_HANDLE DriverHandle) {
    EFI_STATUS                            Status;
    UINTN                                 HandleCount = 0;
    UINTN                                 Index;
    UINTN                                 OpenInfoIndex;
    EFI_HANDLE                            *Handles = NULL;
    MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL       *Fs;
    MY_EFI_BLOCK_IO_PROTOCOL                 *BlockIo;
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY   *OpenInfo;
    UINTN                                 OpenInfoCount;
    EFI_HANDLE                            DriverHandleList[2];

    Status = boot_call5_wrapper(gBS->LocateHandleBuffer,
                                 ByProtocol,
                                 &gMyEfiDiskIoProtocolGuid,
                                 NULL,
                                 &HandleCount,
                                 &Handles);
    if (EFI_ERROR(Status) || HandleCount == 0)
        return;

    for (Index = 0; Index < HandleCount; Index++) {

        Status = boot_call3_wrapper(gBS->HandleProtocol,
                                     Handles[Index],
                                     &gMyEfiBlockIoProtocolGuid,
                                     (VOID **) &BlockIo);
        if (EFI_ERROR (Status))
            continue;
        if (BlockIo->Media == NULL || !BlockIo->Media->LogicalPartition)
            continue;

        Status = boot_call3_wrapper(gBS->HandleProtocol,
                                     Handles[Index],
                                     &gMyEfiSimpleFileSystemProtocolGuid,
                                     (VOID **) &Fs);
        if (Status == EFI_SUCCESS)
            continue;

        Status = boot_call4_wrapper(gBS->OpenProtocolInformation,
                                     Handles[Index],
                                     &gMyEfiDiskIoProtocolGuid,
                                     &OpenInfo,
                                     &OpenInfoCount);
        if (EFI_ERROR (Status))
            continue;
        DriverHandleList[1] = NULL;
        for (OpenInfoIndex = 0; OpenInfoIndex < OpenInfoCount; OpenInfoIndex++) {
            if ((OpenInfo[OpenInfoIndex].Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) == EFI_OPEN_PROTOCOL_BY_DRIVER) {
                Status = boot_call3_wrapper(gBS->DisconnectController,
                                             Handles[Index],
                                             OpenInfo[OpenInfoIndex].AgentHandle,
                                             NULL);
                if (!(EFI_ERROR(Status))) {
                    DriverHandleList[0] = DriverHandle;
                    boot_call4_wrapper(gBS->ConnectController,
                                        Handles[Index],
                                        DriverHandleList,
                                        NULL,
                                        FALSE);
                }  
            }  
        }  
        MyFreePool (OpenInfo);
    }
    MyFreePool(Handles);
}  

/**
 * Count non-hidden .efi candidates as they are discovered, before StartEFIImage
 * returns. A positive count therefore means a directory yielded matching files.
 * It triggers a controller connection and rescan even if firmware rejected a
 * particular driver.
 *
 * The counter is incremented before StartEFIImage, and its EFI_STATUS is not
 * used to decrement the count. It therefore measures candidate filenames,
 * not successfully bound drivers. In particular, a TRUE return from
 * LoadDrivers must not be used as evidence that any filesystem became
 * readable or that a driver passed firmware authentication.
 */
static UINTN ScanDriverDir(IN CHAR16 *Path)
{
    EFI_STATUS              Status;
    BOOT_DIR_ITER          DirIter;
    UINTN                   NumFound = 0;
    EFI_FILE_INFO           *DirEntry;
    CHAR16                  *FileName;
    CHAR16                  *ProgressMessage;

    CleanUpPathNameSlashes(Path);
    ProgressMessage = PoolPrint(L"Scanning EFI driver directory: %s", Path);
    BootStartupProgressReport(ProgressMessage, BOOT_PROGRESS_ACTIVE);
    MyFreePool(ProgressMessage);

    DirIterOpen(SelfRootDir, Path, &DirIter);
    while (DirIterNext(&DirIter, 2, LOADER_MATCH_PATTERNS, &DirEntry)) {
        if (DirEntry->FileName[0] == '.')
            continue;    

        FileName = PoolPrint(L"%s\\%s", Path, DirEntry->FileName);
        NumFound++;
        ProgressMessage = PoolPrint(L"Loading EFI image: %s", DirEntry->FileName);
        BootStartupProgressReport(ProgressMessage, BOOT_PROGRESS_ACTIVE);
        MyFreePool(ProgressMessage);
        Status = StartEFIImage(SelfVolume, FileName, L"", DirEntry->FileName, 0, FALSE, TRUE);
        if (EFI_ERROR(Status)) {
            ProgressMessage = PoolPrint(L"EFI image load failed: %s (status %lu)",
                                        DirEntry->FileName, (UINT64)Status);
            BootStartupProgressReport(ProgressMessage, BOOT_PROGRESS_ERROR);
        } else {
            /*
             * This is the EFI image load result, not proof that its binding
             * protocol mounted any filesystem. No mount success is inferred.
             */
            ProgressMessage = PoolPrint(L"EFI image load accepted: %s", DirEntry->FileName);
            BootStartupProgressReport(ProgressMessage, BOOT_PROGRESS_OK);
        }
        MyFreePool(ProgressMessage);
        MyFreePool(DirEntry);
        MyFreePool(FileName);
    }  
    Status = DirIterClose(&DirIter);
    if ((Status != EFI_NOT_FOUND) && (Status != EFI_INVALID_PARAMETER)) {
        FileName = PoolPrint(L"while scanning the %s directory", Path);
        CheckError(Status, FileName);
        MyFreePool(FileName);
    }
    return (NumFound);
}  

/**
 * Resolve the built-in driver directories relative to BOOT's own path, then scan
 * configured additional directories. Sum the numbers of matching files and
 * connect controllers when the sum is nonzero. efi_main uses the returned Boolean
 * to decide whether to scan volumes again.
 *
 * Its Boolean is derived from filenames encountered across built-in and
 * configured directories. No invariant equates this number with successful
 * LoadImage, StartImage, driver binding, newly installed filesystem protocols,
 * or valid filesystems. A rescan can therefore observe an unchanged volume
 * inventory even when LoadDrivers returns TRUE. A caller needing confirmed
 * readability must inspect an actual post-connection volume and perform a
 * read, not reinterpret this return value as a successful mount count.
 */
BOOLEAN LoadDrivers(VOID) {
    CHAR16        *Directory, *SelfDirectory;
    UINTN         i = 0, Length, NumFound = 0;
    EFI_STATUS    ConnectStatus;

    LOG(1, LOG_LINE_SEPARATOR, L"Loading drivers");

    while ((Directory = FindCommaDelimited(DRIVER_DIRS, i++)) != NULL) {
        SelfDirectory = SelfDirPath ? StrDuplicate(SelfDirPath) : NULL;
        CleanUpPathNameSlashes(SelfDirectory);
        MergeStrings(&SelfDirectory, Directory, L'\\');
        NumFound += ScanDriverDir(SelfDirectory);
        MyFreePool(Directory);
        MyFreePool(SelfDirectory);
    }

    i = 0;
    while ((Directory = FindCommaDelimited(GlobalConfig.DriverDirs, i++)) != NULL) {
        CleanUpPathNameSlashes(Directory);
        Length = StrLen(Directory);
        if (Length > 0) {
            NumFound += ScanDriverDir(Directory);
        }  
        MyFreePool(Directory);
    }  

    if (NumFound > 0) {
        BootStartupProgressReport(L"Connecting EFI controllers", BOOT_PROGRESS_ACTIVE);
        ConnectStatus = ConnectAllDriversToAllControllers();
        BootStartupProgressReport(
            EFI_ERROR(ConnectStatus)
                ? L"EFI controller connection reported an error"
                : L"EFI controller connection pass completed",
            EFI_ERROR(ConnectStatus) ? BOOT_PROGRESS_ERROR : BOOT_PROGRESS_OK);
    } else {
        BootStartupProgressReport(L"No additional EFI driver files found", BOOT_PROGRESS_INFO);
    }
    return (NumFound > 0);
}  
