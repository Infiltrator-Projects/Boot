/*
 * Loads and binds firmware filesystem drivers through UEFI protocols.
 * Drivers may expose previously unreadable partitions as Simple File System
 * handles, and reconnection may affect pre-existing controller bindings.
 * This is the current EFI-driver path, not a Common V2 hosted .so ABI.
 * Driver filename counts are not successful image-start or mount counts.
 * Controller reconnection changes global firmware protocol topology and may
 * disturb existing bindings; a subsequent volume rescan observes the result
 * rather than proving it from the initial directory enumeration.
 */

#include "driver_support.h"
#include "lib.h"
#include "mystrings.h"
#include "screen.h"
#include "launch_efi.h"
#include "log.h"
#include "../include/refit_call_wrapper.h"

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
 * may change as drivers bind, so a snapshot must not be reused indefinitely.
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

  Status = refit_call5_wrapper(BS->LocateHandleBuffer,
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

    Status = refit_call3_wrapper(BS->ProtocolsPerHandle,
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

        Status = refit_call4_wrapper(BS->OpenProtocolInformation,
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

            if (DriverBindingHandle == NULL && OpenInfo[OpenInfoIndex].ControllerHandle == ControllerHandle) {
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
 * Enumerate firmware handles and connect device controllers that lack
 * discovered parents. The classification array comes from a separate
 * handle-database snapshot. Resolve the selected controller's index inside
 * that snapshot instead of indexing it by the outer iteration. Controller
 * connection can change the handle set between snapshots.
 */
EFI_STATUS ConnectAllDriversToAllControllers(VOID)
{
    EFI_STATUS           Status;
    UINTN                AllHandleCount;
    EFI_HANDLE           *AllHandleBuffer;
    UINTN                Index;
    UINTN                HandleCount;
    EFI_HANDLE           *HandleBuffer;
    UINT32               *HandleType;
    UINTN                HandleIndex;
    UINT32               ControllerIndex;
    BOOLEAN              Parent;
    BOOLEAN              Device;

    Status = refit_call5_wrapper(gBS->LocateHandleBuffer,
                                 AllHandles,
                                 NULL,
                                 NULL,
                                 &AllHandleCount,
                                 &AllHandleBuffer);
    if (EFI_ERROR(Status))
        return Status;

    for (Index = 0; Index < AllHandleCount; Index++) {

        Status = LibScanHandleDatabase(NULL,
                                       NULL,
                                       AllHandleBuffer[Index],
                                       &ControllerIndex,
                                       &HandleCount,
                                       &HandleBuffer,
                                       &HandleType);
        if (EFI_ERROR (Status))
            goto Done;

        if (ControllerIndex == 0xffffffff || ControllerIndex >= HandleCount) {
            MyFreePool(HandleBuffer);
            MyFreePool(HandleType);
            continue;
        }

        Device = TRUE;
        if (HandleType[ControllerIndex] & EFI_HANDLE_TYPE_DRIVER_BINDING_HANDLE)
            Device = FALSE;
        if (HandleType[ControllerIndex] & EFI_HANDLE_TYPE_IMAGE_HANDLE)
            Device = FALSE;

        if (Device) {
            Parent = FALSE;
            for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {
                if (HandleType[HandleIndex] & EFI_HANDLE_TYPE_PARENT_HANDLE)
                    Parent = TRUE;
            }  

            if (!Parent && (HandleType[ControllerIndex] & EFI_HANDLE_TYPE_DEVICE_HANDLE)) {
                MY_EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
                EFI_STATUS ProbeStatus;

                /*
                 * The per-driver binding pass already probed accessible
                 * partition handles. A handle with Block I/O is itself a
                 * discovered storage controller, not an unbound bus to
                 * recursively initialise a second time. Reconnecting it
                 * can repeatedly invoke a filesystem driver that has
                 * correctly refused unsupported media. Continue connecting
                 * non-Block-I/O controllers to discover further devices.
                 */
                ProbeStatus = refit_call3_wrapper(BS->HandleProtocol,
                                                  AllHandleBuffer[Index],
                                                  &gMyEfiBlockIoProtocolGuid,
                                                  (VOID **)&BlockIo);
                if (!EFI_ERROR(ProbeStatus) && BlockIo != NULL &&
                    BlockIo->Media != NULL) {
                    LOG(3, LOG_LINE_NORMAL,
                        L"Skipping recursive reconnection of an existing Block I/O device");
                } else {
                    Status = refit_call4_wrapper(BS->ConnectController,
                                                 AllHandleBuffer[Index],
                                                 NULL,
                                                 NULL,
                                                 TRUE);
                }
            }
        }

        MyFreePool (HandleBuffer);
        MyFreePool (HandleType);
    }

Done:
    MyFreePool (AllHandleBuffer);
    return Status;
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

    Status = refit_call5_wrapper(gBS->LocateHandleBuffer,
                                 ByProtocol,
                                 &gMyEfiDiskIoProtocolGuid,
                                 NULL,
                                 &HandleCount,
                                 &Handles);
    if (EFI_ERROR(Status) || HandleCount == 0)
        return;

    for (Index = 0; Index < HandleCount; Index++) {

        Status = refit_call3_wrapper(gBS->HandleProtocol,
                                     Handles[Index],
                                     &gMyEfiBlockIoProtocolGuid,
                                     (VOID **) &BlockIo);
        if (EFI_ERROR (Status))
            continue;
        if (BlockIo->Media == NULL || !BlockIo->Media->LogicalPartition)
            continue;

        Status = refit_call3_wrapper(gBS->HandleProtocol,
                                     Handles[Index],
                                     &gMyEfiSimpleFileSystemProtocolGuid,
                                     (VOID **) &Fs);
        if (Status == EFI_SUCCESS)
            continue;

        Status = refit_call4_wrapper(gBS->OpenProtocolInformation,
                                     Handles[Index],
                                     &gMyEfiDiskIoProtocolGuid,
                                     &OpenInfo,
                                     &OpenInfoCount);
        if (EFI_ERROR (Status))
            continue;
        DriverHandleList[1] = NULL;
        for (OpenInfoIndex = 0; OpenInfoIndex < OpenInfoCount; OpenInfoIndex++) {
            if ((OpenInfo[OpenInfoIndex].Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) == EFI_OPEN_PROTOCOL_BY_DRIVER) {
                Status = refit_call3_wrapper(gBS->DisconnectController,
                                             Handles[Index],
                                             OpenInfo[OpenInfoIndex].AgentHandle,
                                             NULL);
                if (!(EFI_ERROR(Status))) {
                    DriverHandleList[0] = DriverHandle;
                    refit_call4_wrapper(gBS->ConnectController,
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
    REFIT_DIR_ITER          DirIter;
    UINTN                   NumFound = 0;
    EFI_FILE_INFO           *DirEntry;
    CHAR16                  *FileName;

    CleanUpPathNameSlashes(Path);

    DirIterOpen(SelfRootDir, Path, &DirIter);
    while (DirIterNext(&DirIter, 2, LOADER_MATCH_PATTERNS, &DirEntry)) {
        if (DirEntry->FileName[0] == '.')
            continue;    

        FileName = PoolPrint(L"%s\\%s", Path, DirEntry->FileName);
        NumFound++;
        Status = StartEFIImage(SelfVolume, FileName, L"", DirEntry->FileName, 0, FALSE, TRUE);
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

    if (NumFound > 0)
        ConnectAllDriversToAllControllers();
    return (NumFound > 0);
}  
