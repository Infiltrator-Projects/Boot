/*
 * Functions related to drivers. Original copyright notices below....
 */
/*
 * Copyright (c) 2006 - 2011, Intel Corporation. All rights reserved.<BR>
 * This program and the accompanying materials are licensed and made available under
 * the terms and conditions of the BSD License that accompanies this distribution.
 * The full text of the license may be found at
 * http://opensource.org/licenses/bsd-license.php.
 *
 * THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
 * WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
 *
 */
/*
 * refit/main.c
 * Main code for the boot menu
 *
 * Copyright (c) 2006-2010 Christoph Pfisterer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Christoph Pfisterer nor the names of the
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Modifications copyright (c) 2012-2017 Roderick W. Smith
 *
 * Modifications distributed under the terms of the GNU General Public
 * License (GPL) version 3 (GPLv3), or (at your option) any later version.
 *
 */
/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "driver_support.h"
#include "lib.h"
#include "mystrings.h"
#include "screen.h"
#include "launch_efi.h"
#include "log.h"
#include "../include/refit_call_wrapper.h"

/* Firmware-driver progress is Boot-owned and RAM-only. A live marker precedes
 * each potentially blocking UEFI operation; the graphical summary is shown
 * if startup reaches the menu. Console output may be hidden by firmware, and
 * no trace survives a reset. Neither ESP nor NVRAM is written. */
static UINTN DiagDriverAttempts, DiagDriverLoaded, DiagBindAttempts;
static UINTN DiagNewFilesystems, DiagReadableVolumes;
static EFI_STATUS DiagLastStatus = EFI_SUCCESS;
static BOOLEAN DiagLastFinished;
static CHAR16 DiagLastName[64];
static CHAR16 *DiagLastStage = L"not started";

VOID BootDriverDiagnosticProgress(IN CHAR16 *Stage, IN CHAR16 *Name,
                                  IN BOOLEAN Finished, IN EFI_STATUS Status) {
    UINTN i = 0;
    if (!GlobalConfig.DriverDiagnostics)
        return;
    DiagLastStage = Stage ? Stage : L"unknown";
    if (Name != NULL) {
        while (Name[i] && i + 1 < sizeof(DiagLastName) / sizeof(CHAR16)) {
            DiagLastName[i] = Name[i];
            ++i;
        }
        DiagLastName[i] = 0;
    }
    DiagLastFinished = Finished;
    DiagLastStatus = Status;
    if (Finished)
        Print(L"[Boot EFI] %s: %s returned %r\n", DiagLastName, DiagLastStage, Status);
    else
        Print(L"[Boot EFI] %s: %s begin\n", DiagLastName, DiagLastStage);
}

VOID BootDriverDiagnosticVolumes(VOID) {
    UINTN Index;
    if (!GlobalConfig.DriverDiagnostics)
        return;
    DiagReadableVolumes = 0;
    for (Index = 0; Index < VolumesCount; ++Index)
        if (Volumes[Index] != NULL && Volumes[Index]->IsReadable) {
            ++DiagReadableVolumes;
            if (Index < 32)
                Print(L"[Boot EFI] volume %d: %s | filesystem %s | readable\\n",
                      Index, Volumes[Index]->VolName ? Volumes[Index]->VolName : L"unnamed",
                      Volumes[Index]->FsName ? Volumes[Index]->FsName : L"unspecified");
        }
    BootDriverDiagnosticProgress(L"volume rescan", L"all volumes", TRUE, EFI_SUCCESS);
}

/* The diagnostic page owns layout; deliver short lines, never a sprawling
 * status bar. Volumes are firmware-discovered, format-neutral evidence and
 * are deliberately not called 'OFS mounted' solely from driver presence. */
CHAR16 *BootGetDriverDiagnosticLine(IN UINTN Line) {
    UINTN Index, Seen = 0;
    CHAR16 *Result;
    if (!GlobalConfig.DriverDiagnostics)
        return NULL;
    switch (Line) {
        case 0:
            return PoolPrint(L"External drivers started: %d of %d",
                             DiagDriverLoaded, DiagDriverAttempts);
        case 1:
            return PoolPrint(L"Controller connects attempted: %d", DiagBindAttempts);
        case 2:
            return PoolPrint(L"New EFI filesystem protocols: %d", DiagNewFilesystems);
        case 3:
            return PoolPrint(L"Readable volumes after rescan: %d", DiagReadableVolumes);
        case 4:
            return PoolPrint(L"Last stage: %s", DiagLastStage);
        case 5:
            return PoolPrint(L"Last image/device: %s", DiagLastName);
        case 6:
            if (DiagLastFinished)
                return PoolPrint(L"Last result: %r", DiagLastStatus);
            return PoolPrint(L"Last result: operation did not complete");
        default:
            break;
    }
    /* A bounded sample of readable volumes. No disk I/O occurs here; this
     * only presents the volume information already collected by Boot. */
    if (Line < 7 || Line >= 15)
        return NULL;
    for (Index = 0; Index < VolumesCount; ++Index) {
        if (Volumes[Index] == NULL || !Volumes[Index]->IsReadable)
            continue;
        if (Seen++ == Line - 7) {
            Result = PoolPrint(L"Volume %d: %s", Index,
                               Volumes[Index]->VolName ?
                               Volumes[Index]->VolName : L"(unnamed)");
            if (Result != NULL && StrLen(Result) > 50)
                Result[50] = 0;
            return Result;
        }
    }
    return NULL;
}

#if defined (EFIX64)
#define DRIVER_DIRS             L"drivers,drivers_x64"
#elif defined (EFI32)
#define DRIVER_DIRS             L"drivers,drivers_ia32"
#elif defined (EFIAARCH64)
#define DRIVER_DIRS             L"drivers,drivers_aa64"
#else
#define DRIVER_DIRS             L"drivers"
#endif

// Following "global" constants are from EDK2's AutoGen.c....
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
#else /* Make with Tianocore */
#define MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
#define MY_EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL
#define MY_EFI_BLOCK_IO_PROTOCOL EFI_BLOCK_IO_PROTOCOL
#endif

/* LibScanHandleDatabase() is used by rEFInd's driver-loading code (inherited
 * from rEFIt), but has not been implemented in GNU-EFI and seems to have been
 * dropped from current versions of the Tianocore library. This function was
 * taken from http://git.etherboot.org/?p=mirror/efi/shell/.git;a=commitdiff;h=b1b0c63423cac54dc964c2930e04aebb46a946ec,
 * The original files are copyright 2006-2011 Intel and BSD-licensed. Minor
 * modifications by Roderick Smith are GPLv3.
 */
EFI_STATUS
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

  //
  // Retrieve the list of all handles from the handle database
  //

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
    goto Error;
  }

  for (HandleIndex = 0; HandleIndex < *HandleCount; HandleIndex++) {
    //
    // Assume that the handle type is unknown
    //
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
    //
    // Retrieve the list of all the protocols on each handle
    //

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
        //
        // Retrieve the list of agents that have opened each protocol
        //

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
                //
                // Mark the device handle as being managed by the driver specified by DriverBindingHandle
                //
                (*HandleType)[HandleIndex] |= (EFI_HANDLE_TYPE_DEVICE_HANDLE | EFI_HANDLE_TYPE_CONTROLLER_HANDLE);
                //
                // Mark the DriverBindingHandle as being a driver that is managing at least one controller
                //
                if (DriverBindingHandleIndexValid) {
                  (*HandleType)[*DriverBindingHandleIndex] |= EFI_HANDLE_TYPE_DEVICE_DRIVER;
                }
              }

              if ((
                    OpenInfo[OpenInfoIndex].Attributes & EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                ) == EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                  ) {
                //
                // Mark the DriverBindingHandle as being a driver that is managing at least one child controller
                //
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
} /* EFI_STATUS LibScanHandleDatabase() */

#ifdef __MAKEWITH_GNUEFI
/* Modified from EDK2 function of a similar name; original copyright Intel &
 * BSD-licensed; modifications by Roderick Smith are GPLv3. */
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
        //
        // Scan the handle database
        //
        Status = LibScanHandleDatabase(NULL,
                                       NULL,
                                       AllHandleBuffer[Index],
                                       NULL,
                                       &HandleCount,
                                       &HandleBuffer,
                                       &HandleType);
        if (EFI_ERROR (Status))
            goto Done;

        Device = TRUE;
        if (HandleType[Index] & EFI_HANDLE_TYPE_DRIVER_BINDING_HANDLE)
            Device = FALSE;
        if (HandleType[Index] & EFI_HANDLE_TYPE_IMAGE_HANDLE)
            Device = FALSE;

        if (Device) {
            Parent = FALSE;
            for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {
                if (HandleType[HandleIndex] & EFI_HANDLE_TYPE_PARENT_HANDLE)
                    Parent = TRUE;
            } // for

            if (!Parent) {
                if (HandleType[Index] & EFI_HANDLE_TYPE_DEVICE_HANDLE) {
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
} /* EFI_STATUS ConnectAllDriversToAllControllers() */
#else
EFI_STATUS ConnectAllDriversToAllControllers(VOID) {
    BdsLibConnectAllDriversToAllControllers();
    return 0;
}
#endif

/*
 * ConnectFilesystemDriver() is modified from DisconnectInvalidDiskIoChildDrivers()
 * in Clover (https://sourceforge.net/projects/cloverefiboot/), which is derived
 * from rEFIt. The refit/main.c file from which this function was taken continues
 * to bear rEFIt's original copyright/licence notice (BSD); modifications by
 * Roderick Smith (2016) are GPLv3.
 */
/**
 * Some UEFI's (like HPQ EFI from HP notebooks) have DiskIo protocols
 * opened BY_DRIVER (by Partition driver in HP case) even when no file system
 * is produced from this DiskIo. This then blocks our FS drivers from connecting
 * and producing file systems.
 * To fix it: we will disconnect drivers that connected to DiskIo BY_DRIVER
 * if this is partition volume and if those drivers did not produce file system,
 * then try to connect every unconnected device to the driver whose handle is
 * passed to us. This should have no effect on systems unaffected by this EFI
 * bug/quirk.
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

    //
    // Get all DiskIo handles
    //
    Status = refit_call5_wrapper(gBS->LocateHandleBuffer,
                                 ByProtocol,
                                 &gMyEfiDiskIoProtocolGuid,
                                 NULL,
                                 &HandleCount,
                                 &Handles);
    if (EFI_ERROR(Status) || HandleCount == 0)
        return;

    //
    // Check every DiskIo handle
    //
    for (Index = 0; Index < HandleCount; Index++) {
        //
        // If this is not partition - skip it.
        // This is then whole disk and DiskIo
        // should be opened here BY_DRIVER by Partition driver
        // to produce partition volumes.
        //
        Status = refit_call3_wrapper(gBS->HandleProtocol,
                                     Handles[Index],
                                     &gMyEfiBlockIoProtocolGuid,
                                     (VOID **) &BlockIo);
        if (EFI_ERROR (Status))
            continue;
        if (BlockIo->Media == NULL || !BlockIo->Media->LogicalPartition)
            continue;

        //
        // If SimpleFileSystem is already produced - skip it, this is ok
        //
        Status = refit_call3_wrapper(gBS->HandleProtocol,
                                     Handles[Index],
                                     &gMyEfiSimpleFileSystemProtocolGuid,
                                     (VOID **) &Fs);
        if (Status == EFI_SUCCESS)
            continue;

        //
        // If no SimpleFileSystem on this handle but DiskIo is opened BY_DRIVER
        // then disconnect this connection and try to connect our driver to it
        //
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
                BootDriverDiagnosticProgress(L"compat DisconnectController",
                                             L"DiskIo partition", FALSE, EFI_SUCCESS);
                Status = refit_call3_wrapper(gBS->DisconnectController,
                                             Handles[Index],
                                             OpenInfo[OpenInfoIndex].AgentHandle,
                                             NULL);
                BootDriverDiagnosticProgress(L"compat DisconnectController",
                                             L"DiskIo partition", TRUE, Status);
                if (!(EFI_ERROR(Status))) {
                    DriverHandleList[0] = DriverHandle;
                    BootDriverDiagnosticProgress(L"compat ConnectController",
                                                 L"DiskIo partition", FALSE, EFI_SUCCESS);
                    Status = refit_call4_wrapper(gBS->ConnectController,
                                                 Handles[Index],
                                                 DriverHandleList,
                                                 NULL,
                                                 FALSE);
                    BootDriverDiagnosticProgress(L"compat ConnectController",
                                                 L"DiskIo partition", TRUE, Status);
                } // if
            } // if
        } // for
        MyFreePool (OpenInfo);
    }
    MyFreePool(Handles);
} // VOID ConnectFilesystemDriver()

// Bundled filesystem drivers may support GPT/MBR partitions, optical media,
// or native whole-disk layouts (such as Amiga RDB). They expose standard UEFI
// protocols and decide independently whether they support each block device.
//
// Restrict binding to media-bearing BlockIo handles with no filesystem;
// never reconnect unrelated firmware controllers. In particular, do not
// require LogicalPartition: it would prevent native whole-disk drivers from
// discovering their own on-disk partition maps.
static VOID ConnectFilesystemBlockDevices(VOID) {
    EFI_STATUS Status;
    EFI_HANDLE *Handles = NULL;
    UINTN Count = 0, Index, Attempted = 0, Connected = 0;
    MY_EFI_BLOCK_IO_PROTOCOL *BlockIo;
    MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;

    Status = refit_call5_wrapper(gBS->LocateHandleBuffer, ByProtocol,
                                 &gMyEfiBlockIoProtocolGuid, NULL,
                                 &Count, &Handles);
    if (EFI_ERROR(Status)) {
        BootDriverDiagnosticProgress(L"locate BlockIo", L"all controllers", TRUE, Status);
        return;
    }
    BootDriverDiagnosticProgress(L"locate BlockIo", L"all controllers", TRUE, EFI_SUCCESS);

    for (Index = 0; Index < Count; Index++) {
        BlockIo = NULL;
        Status = refit_call3_wrapper(gBS->HandleProtocol, Handles[Index],
                                     &gMyEfiBlockIoProtocolGuid,
                                     (VOID **)&BlockIo);
        if (EFI_ERROR(Status) || BlockIo == NULL || BlockIo->Media == NULL ||
            !BlockIo->Media->MediaPresent)
            continue;

        Fs = NULL;
        Status = refit_call3_wrapper(gBS->HandleProtocol, Handles[Index],
                                     &gMyEfiSimpleFileSystemProtocolGuid,
                                     (VOID **)&Fs);
        if (!EFI_ERROR(Status))
            continue; // Already mounted by firmware or another driver.

        Attempted++;
        if (GlobalConfig.DriverDiagnostics) {
            ++DiagBindAttempts;
            BootDriverDiagnosticProgress(L"ConnectController", L"BlockIo handle", FALSE, EFI_SUCCESS);
        }
        Status = refit_call4_wrapper(gBS->ConnectController, Handles[Index],
                                     NULL, NULL, FALSE);
        if (!EFI_ERROR(Status))
            Connected++;
        if (GlobalConfig.DriverDiagnostics) {
            EFI_STATUS FsStatus;
            Fs = NULL;
            FsStatus = refit_call3_wrapper(gBS->HandleProtocol, Handles[Index],
                                         &gMyEfiSimpleFileSystemProtocolGuid,
                                         (VOID **)&Fs);
            if (!EFI_ERROR(FsStatus))
                ++DiagNewFilesystems;
            BootDriverDiagnosticProgress(L"ConnectController", L"BlockIo handle", TRUE, Status);
        }
    }

    MyFreePool(Handles);
    LOG(1, LOG_LINE_NORMAL,
        L"Filesystem block-device binding: attempted %d, connected %d",
        Attempted, Connected);
} // static VOID ConnectFilesystemBlockDevices()

// Scan a directory for drivers.
// Originally from rEFIt's main.c (BSD), but modified since then (GPLv3).
static UINTN ScanDriverDir(IN CHAR16 *Path)
{
    EFI_STATUS              Status;
    REFIT_DIR_ITER          DirIter;
    UINTN                   NumFound = 0;
    EFI_FILE_INFO           *DirEntry;
    CHAR16                  *FileName;

    CleanUpPathNameSlashes(Path);
    // look through contents of the directory
    DirIterOpen(SelfRootDir, Path, &DirIter);
    while (DirIterNext(&DirIter, 2, LOADER_MATCH_PATTERNS, &DirEntry)) {
        if (DirEntry->FileName[0] == '.')
            continue;   // skip this

        FileName = PoolPrint(L"%s\\%s", Path, DirEntry->FileName);
        if (GlobalConfig.DriverDiagnostics) {
            ++DiagDriverAttempts;
            BootDriverDiagnosticProgress(L"load driver", DirEntry->FileName, FALSE, EFI_SUCCESS);
        }
        Status = StartEFIImage(SelfVolume, FileName, L"", DirEntry->FileName, 0, FALSE, TRUE, FALSE);
        if (!EFI_ERROR(Status)) {
            NumFound++;
            if (GlobalConfig.DriverDiagnostics)
                ++DiagDriverLoaded;
        }
        BootDriverDiagnosticProgress(L"load driver", DirEntry->FileName, TRUE, Status);
        MyFreePool(DirEntry);
        MyFreePool(FileName);
    } // while
    Status = DirIterClose(&DirIter);
    if ((Status != EFI_NOT_FOUND) && (Status != EFI_INVALID_PARAMETER)) {
        FileName = PoolPrint(L"while scanning the %s directory", Path);
        CheckError(Status, FileName);
        MyFreePool(FileName);
    }
    return (NumFound);
} // static UINTN ScanDriverDir()


// Load all EFI drivers from rEFInd's "drivers" subdirectory and from the
// directories specified by the user in the "scan_driver_dirs" configuration
// file line.
// Originally from rEFIt's main.c (BSD), but modified since then (GPLv3).
// Returns TRUE if any drivers are loaded, FALSE otherwise.
BOOLEAN LoadDrivers(VOID) {
    CHAR16        *Directory, *SelfDirectory;
    UINTN         i = 0, Length, NumFound = 0;

    LOG(1, LOG_LINE_SEPARATOR, L"Loading drivers");
    BootDriverDiagnosticProgress(L"driver directory discovery", L"external drivers", FALSE, EFI_SUCCESS);
    // load drivers from the subdirectories of rEFInd's home directory specified
    // in the DRIVER_DIRS constant.
    while ((Directory = FindCommaDelimited(DRIVER_DIRS, i++)) != NULL) {
        SelfDirectory = SelfDirPath ? StrDuplicate(SelfDirPath) : NULL;
        CleanUpPathNameSlashes(SelfDirectory);
        MergeStrings(&SelfDirectory, Directory, L'\\');
        NumFound += ScanDriverDir(SelfDirectory);
        MyFreePool(Directory);
        MyFreePool(SelfDirectory);
    }

    // Scan additional user-specified driver directories....
    i = 0;
    while ((Directory = FindCommaDelimited(GlobalConfig.DriverDirs, i++)) != NULL) {
        CleanUpPathNameSlashes(Directory);
        Length = StrLen(Directory);
        if (Length > 0) {
            NumFound += ScanDriverDir(Directory);
        } // if
        MyFreePool(Directory);
    } // while

    // Any number of bundled filesystem drivers can coexist in drivers_x64.
    // Their Supported() checks decide which block-device handles to bind.
    // Preserve the broad firmware pass for explicitly configured non-filesystem
    // drivers that may expose other protocols.
    if (NumFound > 0) {
        if (GlobalConfig.DriverDirs != NULL && GlobalConfig.DriverDirs[0] != 0)
            ConnectAllDriversToAllControllers();
        else
            ConnectFilesystemBlockDevices();
    }
    BootDriverDiagnosticProgress(L"driver directory discovery", L"external drivers", TRUE, EFI_SUCCESS);
    return (NumFound > 0);
} /* BOOLEAN LoadDrivers() */
