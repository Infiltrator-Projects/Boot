/*
 * EDK driver component-name protocol interfaces used for filesystem driver
 * identity. Strings describe the controller/driver; names are not evidence
 * of successful mount or authenticated image loading.
 */
 















 












#ifndef __REFIND_EFI_COMPONENT_NAME_H__
#define __REFIND_EFI_COMPONENT_NAME_H__

 
 
 
#define REFIND_EFI_COMPONENT_NAME_PROTOCOL_GUID \
  { \
    0x107a772c, 0xd5e1, 0x11d4, {0x9a, 0x46, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d } \
  }

typedef struct _REFIND_EFI_COMPONENT_NAME_PROTOCOL  REFIND_EFI_COMPONENT_NAME_PROTOCOL;


 





















typedef
EFI_STATUS
(EFI_FUNCTION EFIAPI *REFIND_EFI_COMPONENT_NAME_GET_DRIVER_NAME)(
  IN REFIND_EFI_COMPONENT_NAME_PROTOCOL    *This,
  IN  CHAR8                                *Language,
  OUT CHAR16                               **DriverName
  );


 







































typedef
EFI_STATUS
(EFI_FUNCTION EFIAPI *REFIND_EFI_COMPONENT_NAME_GET_CONTROLLER_NAME)(
  IN  REFIND_EFI_COMPONENT_NAME_PROTOCOL                      *This,
  IN  EFI_HANDLE                                              ControllerHandle,
  IN  EFI_HANDLE                                              ChildHandle        OPTIONAL,
  IN  CHAR8                                                   *Language,
  OUT CHAR16                                                  **ControllerName
  );

 
 
 
 
struct _REFIND_EFI_COMPONENT_NAME_PROTOCOL {
  REFIND_EFI_COMPONENT_NAME_GET_DRIVER_NAME      GetDriverName;
  REFIND_EFI_COMPONENT_NAME_GET_CONTROLLER_NAME  GetControllerName;
   
   
   
   
   
  CHAR8                                   *SupportedLanguages;
};

extern EFI_GUID gEfiComponentNameProtocolGuid;

#endif
