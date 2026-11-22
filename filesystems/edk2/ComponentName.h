/*
 * BOOT / EFI component naming
 *
 * EDK driver component-name protocol interfaces used for filesystem driver
 * identity.
 *
 * Strings describe the controller/driver; names are not evidence of
 * successful mount or authenticated image loading.
 */

#ifndef __BOOT_EFI_COMPONENT_NAME_H__
#define __BOOT_EFI_COMPONENT_NAME_H__

#define BOOT_EFI_COMPONENT_NAME_PROTOCOL_GUID \
  { \
    0x107a772c, 0xd5e1, 0x11d4, {0x9a, 0x46, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d } \
  }

typedef struct _BOOT_EFI_COMPONENT_NAME_PROTOCOL  BOOT_EFI_COMPONENT_NAME_PROTOCOL;

/*
 * DriverName receives a pointer to text owned by the driver. The FSW
 * implementation selects its compiled-in name for the three-letter language
 * code eng and returns EFI_UNSUPPORTED for other requests.
 */
typedef
EFI_STATUS
(EFI_FUNCTION EFIAPI *BOOT_EFI_COMPONENT_NAME_GET_DRIVER_NAME)(
  IN BOOT_EFI_COMPONENT_NAME_PROTOCOL    *This,
  IN  CHAR8                                *Language,
  OUT CHAR16                               **DriverName
  );

typedef
EFI_STATUS
(EFI_FUNCTION EFIAPI *BOOT_EFI_COMPONENT_NAME_GET_CONTROLLER_NAME)(
  IN  BOOT_EFI_COMPONENT_NAME_PROTOCOL                      *This,
  IN  EFI_HANDLE                                              ControllerHandle,
  IN  EFI_HANDLE                                              ChildHandle        OPTIONAL,
  IN  CHAR8                                                   *Language,
  OUT CHAR16                                                  **ControllerName
  );

struct _BOOT_EFI_COMPONENT_NAME_PROTOCOL {
  BOOT_EFI_COMPONENT_NAME_GET_DRIVER_NAME      GetDriverName;
  BOOT_EFI_COMPONENT_NAME_GET_CONTROLLER_NAME  GetControllerName;

  CHAR8                                   *SupportedLanguages;
};

extern EFI_GUID gEfiComponentNameProtocolGuid;

#endif
