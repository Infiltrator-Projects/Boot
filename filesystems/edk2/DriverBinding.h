 






















 












#ifndef _EFI_DRIVER_BINDING_H_
#define _EFI_DRIVER_BINDING_H_

#include <efidevp.h>

#define REFIND_EFI_DRIVER_BINDING_PROTOCOL_GUID \
  { \
    0x18a031ab, 0xb443, 0x4d1a, {0xa5, 0xc0, 0xc, 0x9, 0x26, 0x1e, 0x9f, 0x71} \
  }

#define EFI_FORWARD_DECLARATION(x) typedef struct _##x x

EFI_FORWARD_DECLARATION (REFIND_EFI_DRIVER_BINDING_PROTOCOL);

 

 
 
 
 

#pragma pack(1)

 







typedef struct {
  UINT8 Type;        
                     
                     
                     
                     
                     

  UINT8 SubType;     
                     
                     
                     

  UINT8 Length[2];   
                     

} REFIND_EFI_DEVICE_PATH_PROTOCOL;

#pragma pack()


 

typedef
EFI_STATUS
(EFI_FUNCTION EFIAPI *EFI_DRIVER_BINDING_SUPPORTED) (
  IN REFIND_EFI_DRIVER_BINDING_PROTOCOL            * This,
  IN EFI_HANDLE                             ControllerHandle,
  IN REFIND_EFI_DEVICE_PATH_PROTOCOL               * RemainingDevicePath OPTIONAL
  )
 
















;

typedef
EFI_STATUS
(EFI_FUNCTION EFIAPI *EFI_DRIVER_BINDING_START) (
  IN REFIND_EFI_DRIVER_BINDING_PROTOCOL            * This,
  IN EFI_HANDLE                             ControllerHandle,
  IN REFIND_EFI_DEVICE_PATH_PROTOCOL               * RemainingDevicePath OPTIONAL
  )
 
















;

typedef
EFI_STATUS
(EFI_FUNCTION EFIAPI *EFI_DRIVER_BINDING_STOP) (
  IN REFIND_EFI_DRIVER_BINDING_PROTOCOL            * This,
  IN  EFI_HANDLE                            ControllerHandle,
  IN  UINTN                                 NumberOfChildren,
  IN  EFI_HANDLE                            * ChildHandleBuffer
  )
 
















;

 
 
 
struct _REFIND_EFI_DRIVER_BINDING_PROTOCOL {
  EFI_DRIVER_BINDING_SUPPORTED  Supported;
  EFI_DRIVER_BINDING_START      Start;
  EFI_DRIVER_BINDING_STOP       Stop;
  UINT32                        Version;
  EFI_HANDLE                    ImageHandle;
  EFI_HANDLE                    DriverBindingHandle;
};

extern EFI_GUID gEfiDriverBindingProtocolGuid;

#endif
