 
















#include "gnuefi-helper.h"
#include "DevicePathUtilities.h"
#include "refit_call_wrapper.h"
#include "LegacyBios.h"

EFI_GUID gEfiLegacyBiosProtocolGuid = { 0xdb9a1e3d, 0x45cb, 0x4abb, { 0x85, 0x3b, 0xe5, 0x38, 0x7f, 0xdb, 0x2e, 0x2d }};

 
































CHAR8 *
UnicodeStrToAsciiStr (
  IN      CHAR16              *Source,
  OUT     CHAR8               *Destination
  )
{
  ASSERT (Destination != NULL);
  ASSERT (Source != NULL);
  ASSERT (((UINTN) Source & 0x01) == 0);

   
   
   
  ASSERT ((UINTN) ((CHAR16 *) Destination -  Source) > StrLen (Source));
  ASSERT ((UINTN) ((CHAR8 *) Source - Destination) > StrLen (Source));

 
 
 
 
 
 
 

  while (*Source != '\0') {
     
     
     
     
    ASSERT (*Source < 0x100);
    *(Destination++) = (CHAR8) *(Source++);
  }

  *Destination = '\0';

  return Destination;
}

 















UINTN
AsciiStrLen (
  IN      CONST CHAR8               *String
  )
{
  UINTN                             Length;

  ASSERT (String != NULL);

  for (Length = 0; *String != '\0'; String++, Length++) {
 
 
 
 
 
 
 
  }
  return Length;
}

 














BOOLEAN
EFIAPI
IsDevicePathValid (
  IN CONST EFI_DEVICE_PATH_PROTOCOL *DevicePath,
  IN       UINTN                    MaxSize
  )
{
 
  UINTN Size;
  UINTN NodeLength;

  ASSERT (DevicePath != NULL);

  for (  Size = 0; !IsDevicePathEnd (DevicePath); DevicePath = NextDevicePathNode (DevicePath)) {
    NodeLength = DevicePathNodeLength (DevicePath);
    if (NodeLength < sizeof (EFI_DEVICE_PATH_PROTOCOL)) {
      return FALSE;
    }

    if (MaxSize > 0) {
      Size += NodeLength;
      if (Size + END_DEVICE_PATH_LENGTH > MaxSize) {
        return FALSE;
      }
    }

 
 
 
 
 
 
  }

   
   
   
  return (BOOLEAN) (DevicePathNodeLength (DevicePath) == END_DEVICE_PATH_LENGTH);
}

 












UINTN
EFIAPI
GetDevicePathSize (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  CONST EFI_DEVICE_PATH_PROTOCOL  *Start;

  if (DevicePath == NULL) {
    return 0;
  }

  if (!IsDevicePathValid (DevicePath, 0)) {
    return 0;
  }

   
   
   
  Start = DevicePath;
  while (!IsDevicePathEnd (DevicePath)) {
    DevicePath = NextDevicePathNode (DevicePath);
  }

   
   
   
  return ((UINTN) DevicePath - (UINTN) Start) + DevicePathNodeLength (DevicePath);
}

 



























EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
GetNextDevicePathInstance (
  IN OUT EFI_DEVICE_PATH_PROTOCOL    **DevicePath,
  OUT UINTN                          *Size
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *DevPath;
  EFI_DEVICE_PATH_PROTOCOL  *ReturnValue;
  UINT8                     Temp;

  ASSERT (Size != NULL);

  if (DevicePath == NULL || *DevicePath == NULL) {
    *Size = 0;
    return NULL;
  }

  if (!IsDevicePathValid (*DevicePath, 0)) {
    return NULL;
  }

   
   
   
  DevPath = *DevicePath;
  while (!IsDevicePathEndType (DevPath)) {
    DevPath = NextDevicePathNode (DevPath);
  }

   
   
   
  *Size = ((UINTN) DevPath - (UINTN) (*DevicePath)) + sizeof (EFI_DEVICE_PATH_PROTOCOL);
 
   
   
   
  Temp              = DevPath->SubType;
  DevPath->SubType  = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  ReturnValue       = DuplicateDevicePath (*DevicePath);
  DevPath->SubType  = Temp;

   
   
   
   
  if (DevicePathSubType (DevPath) == END_ENTIRE_DEVICE_PATH_SUBTYPE) {
    *DevicePath = NULL;
  } else {
    *DevicePath = NextDevicePathNode (DevPath);
  }

  return ReturnValue;
}
