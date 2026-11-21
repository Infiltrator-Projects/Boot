/*
 * Apple removable-media protocol GUID and function signatures. This is a
 * vendor-specific firmware interface and must be treated as optional.
 * Its EFIAPI calling convention is part of the firmware ABI.
 */
 





















































#ifndef _APPLE_REMOVABLE_MEDIA_H
#define _APPLE_REMOVABLE_MEDIA_H

 
 
 
 
#define APPLE_REMOVABLE_MEDIA_PROTOCOL_GUID \
  { \
    0x2ea9743a, 0x23d9, 0x425e, { 0x87, 0x2c, 0xf6, 0x15, 0xaa, 0x19, 0x57, 0x88 } \
  }

#define	APPLE_REMOVABLE_MEDIA_PROTOCOL_REVISION		0x00000001

 
typedef struct _APPLE_REMOVABLE_MEDIA_PROTOCOL APPLE_REMOVABLE_MEDIA_PROTOCOL;

 
 
 
typedef
EFI_STATUS
(EFIAPI *APPLE_REMOVABLE_MEDIA_EJECT) (
  IN APPLE_REMOVABLE_MEDIA_PROTOCOL				* This
  )
 











;

 
 
 
typedef
EFI_STATUS
(EFIAPI *APPLE_REMOVABLE_MEDIA_INJECT) (
  IN APPLE_REMOVABLE_MEDIA_PROTOCOL				* This
  )
 











;

 
 
 
typedef
EFI_STATUS
(EFIAPI *APPLE_REMOVABLE_MEDIA_ALLOW_REMOVAL) (
  IN APPLE_REMOVABLE_MEDIA_PROTOCOL				* This
  )
 












;

 
 
 
typedef
EFI_STATUS
(EFIAPI *APPLE_REMOVABLE_MEDIA_PREVENT_REMOVAL) (
  IN APPLE_REMOVABLE_MEDIA_PROTOCOL				* This
  )
 












;

 
 
 
typedef
EFI_STATUS
(EFIAPI *APPLE_REMOVABLE_MEDIA_DETECT_TRAY_STATE) (
  IN APPLE_REMOVABLE_MEDIA_PROTOCOL				* This,
  OUT BOOLEAN									* TrayOpen
  )
 












;

 
 
 
struct _APPLE_REMOVABLE_MEDIA_PROTOCOL {
  UINT32										Revision;
  BOOLEAN										RemovalAllowed;
  /*
   * The protocol exposes operations as firmware callbacks on a particular
   * media handle. RemovalAllowed is reported state. Eject must still return
   * an EFI status for the actual request.
   */
  APPLE_REMOVABLE_MEDIA_EJECT					Eject;
  APPLE_REMOVABLE_MEDIA_INJECT					Inject;
  APPLE_REMOVABLE_MEDIA_ALLOW_REMOVAL			AllowRemoval;
  APPLE_REMOVABLE_MEDIA_PREVENT_REMOVAL			PreventRemoval;
  APPLE_REMOVABLE_MEDIA_DETECT_TRAY_STATE		DetectTrayState;

};

 

#endif

