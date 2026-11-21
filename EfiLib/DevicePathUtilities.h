 


















#ifndef _DEVICE_PATH_UTILITIES_PROTOCOL_H_
#define _DEVICE_PATH_UTILITIES_PROTOCOL_H_

 
 
 
#define EFI_DEVICE_PATH_UTILITIES_PROTOCOL_GUID \
  { \
    0x379be4e, 0xd706, 0x437d, {0xb0, 0x37, 0xed, 0xb8, 0x2f, 0xb7, 0x72, 0xa4}  \
  }

typedef
UINTN
EFIAPI
(EFIAPI *EFI_DEVICE_PATH_UTILS_GET_DEVICE_PATH_SIZE) (
  IN CONST EFI_DEVICE_PATH_PROTOCOL *DevicePath
  )
 











;

typedef
EFI_DEVICE_PATH_PROTOCOL*
EFIAPI
(EFIAPI *EFI_DEVICE_PATH_UTILS_DUP_DEVICE_PATH) (
  IN CONST EFI_DEVICE_PATH_PROTOCOL *DevicePath
  )
 












;

typedef
EFI_DEVICE_PATH_PROTOCOL*
EFIAPI
(EFIAPI *EFI_DEVICE_PATH_UTILS_APPEND_PATH) (
  IN CONST EFI_DEVICE_PATH_PROTOCOL *Src1,
  IN CONST EFI_DEVICE_PATH_PROTOCOL *Src2
  )
 














;

typedef
EFI_DEVICE_PATH_PROTOCOL*
EFIAPI
(EFIAPI *EFI_DEVICE_PATH_UTILS_APPEND_NODE) (
  IN CONST EFI_DEVICE_PATH_PROTOCOL *DevicePath,
  IN CONST EFI_DEVICE_PATH_PROTOCOL *DeviceNode
  )
 














;

typedef
EFI_DEVICE_PATH_PROTOCOL*
EFIAPI
(EFIAPI *EFI_DEVICE_PATH_UTILS_APPEND_INSTANCE) (
  IN CONST EFI_DEVICE_PATH_PROTOCOL *DevicePath,
  IN CONST EFI_DEVICE_PATH_PROTOCOL *DevicePathInstance
  )
 













;

typedef
EFI_DEVICE_PATH_PROTOCOL*
EFIAPI
(EFIAPI *EFI_DEVICE_PATH_UTILS_GET_NEXT_INSTANCE) (
  IN  OUT EFI_DEVICE_PATH_PROTOCOL  **DevicePathInstance,
  OUT UINTN                         *DevicePathInstanceSize
  )
 

















;

typedef
BOOLEAN
EFIAPI
(EFIAPI *EFI_DEVICE_PATH_UTILS_IS_MULTI_INSTANCE) (
  IN CONST EFI_DEVICE_PATH_PROTOCOL         *DevicePath
  )
 












;

typedef
EFI_DEVICE_PATH_PROTOCOL*
EFIAPI
(EFIAPI *EFI_DEVICE_PATH_UTILS_CREATE_NODE) (
  IN UINT8                          NodeType,
  IN UINT8                          NodeSubType,
  IN UINT16                         NodeLength
  )
 


















;

#endif
