/*
 * EDK/Tiano BDS compatibility bridge for device paths and firmware variables.
 * The module maps the library interfaces required by BOOT's historical BDS
 * code into the selected build environment. Firmware data lengths and EFI
 * error results must remain authoritative.
 */

#ifdef __MAKEWITH_TIANO
#include "../include/tiano_includes.h"
#else
#include "BdsHelper.h"
#include "gnuefi-helper.h"
#endif
#include "../include/refit_call_wrapper.h"
#include "../boot/lib.h"

EFI_GUID EfiDevicePathProtocolGuid = { 0x09576E91, 0x6D3F, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B }};

/**
 * For each path instance, locate the handle matching the longest connected prefix
 * and ask firmware to connect the remaining suffix. Repeating the lookup can then
 * discover a longer prefix. Only the Tiano build dispatches additional drivers
 * when the same handle is returned twice.
 */
/*
 * Walks a firmware device path, connecting controller instances where
 * possible. A structurally sound path need not refer to a present device;
 * returned EFI_STATUS governs whether the attempted connection succeeded.
 */
EFI_STATUS
BdsLibConnectDevicePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePathToConnect
  )
{
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *CopyOfDevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *Instance;
  EFI_DEVICE_PATH_PROTOCOL  *RemainingDevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *Next;
  EFI_HANDLE                Handle;
  EFI_HANDLE                PreviousHandle;
  UINTN                     Size;

  if (DevicePathToConnect == NULL) {
    return EFI_SUCCESS;
  }

  DevicePath        = DuplicateDevicePath (DevicePathToConnect);
  if (DevicePath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  CopyOfDevicePath  = DevicePath;

  do {

    Instance  = GetNextDevicePathInstance (&DevicePath, &Size);
    if (Instance == NULL) {
      MyFreePool (CopyOfDevicePath);
      return EFI_OUT_OF_RESOURCES;
    }

    Next      = Instance;
    while (!IsDevicePathEndType (Next)) {
      Next = NextDevicePathNode (Next);
    }

    SetDevicePathEndNode (Next);

    PreviousHandle = NULL;
    do {

      RemainingDevicePath = Instance;
      Status              = refit_call3_wrapper(gBS->LocateDevicePath, &EfiDevicePathProtocolGuid, &RemainingDevicePath, &Handle);

      if (!EFI_ERROR (Status)) {
#ifdef __MAKEWITH_TIANO
         /*
          * LocateDevicePath returns the longest connected prefix and advances
          * RemainingDevicePath. Seeing the same handle again signals stalled
          * progress. The Tiano build tries the DXE dispatcher before another
          * controller connection.
          */
         if (Handle == PreviousHandle) {

          if (gDS) {
              Status = gDS->Dispatch ();
          } else {
              Status = EFI_NOT_FOUND;
          }
        }
#endif

        if (!EFI_ERROR (Status)) {
          PreviousHandle = Handle;

          refit_call4_wrapper(gBS->ConnectController, Handle, NULL, RemainingDevicePath, FALSE);
        }
      }

    } while (!EFI_ERROR (Status) && !IsDevicePathEnd (RemainingDevicePath));

  } while (DevicePath != NULL);

  if (CopyOfDevicePath != NULL) {
    MyFreePool (CopyOfDevicePath);
  }

  return Status;
}

/**
 * Split a firmware load option into attributes, path-list byte length, a
 * terminated CHAR16 description and optional trailing data. Copy these fields
 * into BDS-owned storage before dropping the variable buffer. Only active options
 * are appended to the caller's list. This legacy decoder expects a structurally
 * valid variable.
 */
/*
 * Decodes a firmware load-option variable; the data is externally supplied
 * and its device path and description cannot be treated as trusted pointers.
 */
BDS_COMMON_OPTION *
BdsLibVariableToOption (
  IN OUT LIST_ENTRY                   *BdsCommonOptionList,
  IN  CHAR16                          *VariableName
  )
{
  UINT32                    Attribute;
  UINT16                    FilePathSize;
  UINT8                     *Variable;
  UINT8                     *TempPtr;
  UINTN                     VariableSize;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  BDS_COMMON_OPTION         *Option;
  VOID                      *LoadOptions;
  UINT32                    LoadOptionsSize;
  CHAR16                    *Description;
  UINT8                     NumOff;
  EFI_GUID EfiGlobalVariableGuid     = { 0x8BE4DF61, 0x93CA, 0x11D2, { 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C }};

  Variable = BdsLibGetVariableAndSize (
              VariableName,
              &EfiGlobalVariableGuid,
              &VariableSize
              );
  if (Variable == NULL) {
    return NULL;
  }

  TempPtr   =  Variable;
  Attribute =  *(UINT32 *) Variable;
  TempPtr   += sizeof (UINT32);

  FilePathSize =  *(UINT16 *) TempPtr;
  TempPtr      += sizeof (UINT16);
  /*
   * The variable layout supplies a terminated UTF-16 description followed by
   * FilePathSize bytes and then optional load data. This legacy parser calls
   * StrSize before comparing the resulting cursor with VariableSize. Its
   * layout assumptions differ from the bounded menu parser in boot/install.c.
   */
  Description = (CHAR16 *) TempPtr;

  TempPtr     += StrSize ((CHAR16 *) TempPtr);

  DevicePath =  (EFI_DEVICE_PATH_PROTOCOL *) TempPtr;
  TempPtr    += FilePathSize;

  LoadOptions     = TempPtr;
  LoadOptionsSize = (UINT32) (VariableSize - (UINTN) (TempPtr - Variable));

  Option = AllocateZeroPool (sizeof (BDS_COMMON_OPTION));
  if (Option == NULL) {
    return NULL;
  }

  Option->Signature   = BDS_LOAD_OPTION_SIGNATURE;
  Option->DevicePath  = AllocateZeroPool (GetDevicePathSize (DevicePath));
  ASSERT(Option->DevicePath != NULL);
  CopyMem (Option->DevicePath, DevicePath, GetDevicePathSize (DevicePath));

  Option->Attribute   = Attribute;
  Option->Description = AllocateZeroPool (StrSize (Description));
  ASSERT(Option->Description != NULL);
  CopyMem (Option->Description, Description, StrSize (Description));

  Option->LoadOptions = AllocateZeroPool (LoadOptionsSize);
  ASSERT(Option->LoadOptions != NULL);
  CopyMem (Option->LoadOptions, LoadOptions, LoadOptionsSize);
  Option->LoadOptionsSize = LoadOptionsSize;

  if (*VariableName == 'B') {
    NumOff = (UINT8) (sizeof (L"Boot") / sizeof(CHAR16) - 1);
    Option->BootCurrent = (UINT16) ((VariableName[NumOff]  -'0') * 0x1000);
    Option->BootCurrent = (UINT16) (Option->BootCurrent + ((VariableName[NumOff+1]-'0') * 0x100));
    Option->BootCurrent = (UINT16) (Option->BootCurrent +  ((VariableName[NumOff+2]-'0') * 0x10));
    Option->BootCurrent = (UINT16) (Option->BootCurrent + ((VariableName[NumOff+3]-'0')));
  }

  if ((Option->Attribute & LOAD_OPTION_ACTIVE) == LOAD_OPTION_ACTIVE) {
    InsertTailList (BdsCommonOptionList, &Option->Link);
    MyFreePool (Variable);
    return Option;
  }

  MyFreePool (Variable);
  MyFreePool (Option);
  return NULL;

}

VOID *
/*
 * Retrieves a variable from EFI Runtime Services with a caller-owned result.
 * The returned size and status, not the variable name, determine validity.
 */
BdsLibGetVariableAndSize (
  IN  CHAR16              *Name,
  IN  EFI_GUID            *VendorGuid,
  OUT UINTN               *VariableSize
  )
{
  EFI_STATUS  Status;
  UINTN       BufferSize;
  VOID        *Buffer;

  Buffer = NULL;

  BufferSize  = 0;
  Status      = refit_call5_wrapper(gRT->GetVariable, Name, VendorGuid, NULL, &BufferSize, Buffer);
  if (Status == EFI_BUFFER_TOO_SMALL) {

    Buffer = AllocateZeroPool (BufferSize);
    if (Buffer == NULL) {
      return NULL;
    }

    Status = refit_call5_wrapper(gRT->GetVariable, Name, VendorGuid, NULL, &BufferSize, Buffer);
    if (EFI_ERROR (Status)) {
      BufferSize = 0;
    }
  }

  *VariableSize = BufferSize;
  return Buffer;
}
