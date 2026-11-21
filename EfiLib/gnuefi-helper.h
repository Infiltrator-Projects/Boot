/*
 * Bridge EDK-style device-path operations to GNU-EFI.
 * GetNextDevicePathInstance allocates one terminated instance and advances
 * the caller's input pointer. Keep the original owning pointer separately
 * when walking an allocated path.
 */

#ifndef __EFILIB_GNUEFI_H
#define __EFILIB_GNUEFI_H

#include "efi.h"
#include "efilib.h"

#define EFI_DEVICE_PATH_PROTOCOL EFI_DEVICE_PATH
#define gRT RT
#define gBS BS
#ifndef CONST
#define CONST
#endif
#define ASSERT_EFI_ERROR(status)  ASSERT(!EFI_ERROR(status))

CHAR8 *
UnicodeStrToAsciiStr (
   IN       CHAR16              *Source,
   OUT      CHAR8               *Destination
);

UINTN
AsciiStrLen (
   IN      CONST CHAR8               *String
);

UINTN
EFIAPI
GetDevicePathSize (
   IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath
);

EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
GetNextDevicePathInstance (
   IN OUT EFI_DEVICE_PATH_PROTOCOL    **DevicePath,
   OUT UINTN                          *Size
);

#endif
