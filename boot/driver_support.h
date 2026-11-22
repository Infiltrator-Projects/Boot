/* Driver binding and controller classification interface.
 * Handle flags describe observed EFI protocol relationships.
 * Binding can change the handle inventory seen by later scans.
 */

#ifndef _DRIVER_SUPPORT
#define _DRIVER_SUPPORT

#ifdef __MAKEWITH_GNUEFI
#include <efi.h>
#else
#include "../include/tiano_includes.h"
#endif
/*
 * LoadDrivers counts candidate driver names, not successfully started EFI
 * drivers. A positive result triggers controller connection and rescan even
 * when firmware rejects a candidate. Connection can change handle bindings.
 */
#include "global.h"
/*
 * These flags describe observed protocol relationships. Image, driver and
 * controller roles can coexist on a single handle, so classification combines
 * the flags before the connection pass examines them.
 */
#define EFI_HANDLE_TYPE_UNKNOWN                     0x000
#define EFI_HANDLE_TYPE_IMAGE_HANDLE                0x001
#define EFI_HANDLE_TYPE_DRIVER_BINDING_HANDLE       0x002
#define EFI_HANDLE_TYPE_DEVICE_DRIVER               0x004
#define EFI_HANDLE_TYPE_BUS_DRIVER                  0x008
#define EFI_HANDLE_TYPE_DRIVER_CONFIGURATION_HANDLE 0x010
#define EFI_HANDLE_TYPE_DRIVER_DIAGNOSTICS_HANDLE   0x020
#define EFI_HANDLE_TYPE_COMPONENT_NAME_HANDLE       0x040
#define EFI_HANDLE_TYPE_DEVICE_HANDLE               0x080
#define EFI_HANDLE_TYPE_PARENT_HANDLE               0x100
#define EFI_HANDLE_TYPE_CONTROLLER_HANDLE           0x200
#define EFI_HANDLE_TYPE_CHILD_HANDLE                0x400

/*
 * Handle classification returns newly allocated parallel handle and flag
 * arrays. HandleCount applies to both arrays; the caller releases them
 * after connection decisions and must not assume every handle is a
 * controller.
 */
EFI_STATUS
LibScanHandleDatabase (
  EFI_HANDLE  DriverBindingHandle, OPTIONAL
  UINT32      *DriverBindingHandleIndex, OPTIONAL
  EFI_HANDLE  ControllerHandle, OPTIONAL
  UINT32      *ControllerHandleIndex, OPTIONAL
  UINTN       *HandleCount,
  EFI_HANDLE  **HandleBuffer,
  UINT32      **HandleType
  );
/*
 * Controller reconnection changes firmware protocol topology and may
 * invalidate old volume handles. ConnectFilesystemDriver is a best-effort
 * VOID operation; LoadDrivers indicates filename candidates, not confirmed
 * mounts.
 */
EFI_STATUS ConnectAllDriversToAllControllers(VOID);
VOID ConnectFilesystemDriver(EFI_HANDLE DriverHandle);
BOOLEAN LoadDrivers(VOID);

#endif
