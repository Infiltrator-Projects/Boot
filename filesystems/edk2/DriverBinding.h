/*
 * BOOT / EFI driver binding API
 *
 * EDK-compatible EFI Driver Binding protocol interface used by filesystem
 * drivers.
 *
 * The callbacks may install or uninstall controller protocols; these are
 * firmware-visible changes with strict handle/lifetime semantics.
 */

#ifndef _EFI_DRIVER_BINDING_H_
#define _EFI_DRIVER_BINDING_H_

#include <efidevp.h>

#define BOOT_EFI_DRIVER_BINDING_PROTOCOL_GUID \
  { \
    0x18a031ab, 0xb443, 0x4d1a, {0xa5, 0xc0, 0xc, 0x9, 0x26, 0x1e, 0x9f, 0x71} \
  }

#define EFI_FORWARD_DECLARATION(x) typedef struct _##x x

EFI_FORWARD_DECLARATION (BOOT_EFI_DRIVER_BINDING_PROTOCOL);

#pragma pack(1)

typedef struct {
  UINT8 Type;        

  UINT8 SubType;     

  UINT8 Length[2];   

} BOOT_EFI_DEVICE_PATH_PROTOCOL;

#pragma pack()
/*
 * Supported is the probe stage. Start establishes the controller binding and
 * publishes the per-volume filesystem protocol. Stop withdraws that interface
 * before freeing the state embedded around it.
 */
typedef
EFI_STATUS
(EFI_FUNCTION EFIAPI *EFI_DRIVER_BINDING_SUPPORTED) (
  IN BOOT_EFI_DRIVER_BINDING_PROTOCOL            * This,
  IN EFI_HANDLE                             ControllerHandle,
  IN BOOT_EFI_DEVICE_PATH_PROTOCOL               * RemainingDevicePath OPTIONAL
  )

;

typedef
EFI_STATUS
(EFI_FUNCTION EFIAPI *EFI_DRIVER_BINDING_START) (
  IN BOOT_EFI_DRIVER_BINDING_PROTOCOL            * This,
  IN EFI_HANDLE                             ControllerHandle,
  IN BOOT_EFI_DEVICE_PATH_PROTOCOL               * RemainingDevicePath OPTIONAL
  )

;

typedef
EFI_STATUS
(EFI_FUNCTION EFIAPI *EFI_DRIVER_BINDING_STOP) (
  IN BOOT_EFI_DRIVER_BINDING_PROTOCOL            * This,
  IN  EFI_HANDLE                            ControllerHandle,
  IN  UINTN                                 NumberOfChildren,
  IN  EFI_HANDLE                            * ChildHandleBuffer
  )

;

struct _BOOT_EFI_DRIVER_BINDING_PROTOCOL {
  EFI_DRIVER_BINDING_SUPPORTED  Supported;
  EFI_DRIVER_BINDING_START      Start;
  EFI_DRIVER_BINDING_STOP       Stop;
  UINT32                        Version;
  EFI_HANDLE                    ImageHandle;
  EFI_HANDLE                    DriverBindingHandle;
};

extern EFI_GUID gEfiDriverBindingProtocolGuid;

#endif
