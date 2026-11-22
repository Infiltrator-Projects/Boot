/*
 * BOOT / Device-path parsing
 *
 * UEFI device-path parsing and text formatting.
 *
 * Firmware-supplied nodes encode type, subtype and byte length. Display
 * formatting does not substitute for bounded binary validation.
 */

#include "Platform.h"

/*
 * Append diagnostic UTF-16 text through a fixed temporary buffer and an
 * EFI pool allocation. The resulting pointer is returned without an EFI_STATUS:
 * ASSERT and size-limit behaviour cannot be mistaken for validation of the
 * underlying untrusted device-path nodes. This is a formatter, not a parser.
 */
CHAR16 *
EFIAPI
MyCatPrint (
  IN OUT POOL_PRINT   *Str,
  IN CHAR16           *Fmt,
  ...
  )
{
  CHAR16  *AppendStr;
  CHAR16  *NewStr;
  VA_LIST Args;
  UINTN   AppendBytes;
  UINTN   UsedBytes;
  UINTN   NeededBytes;

  if (Str == NULL || Fmt == NULL)
    return NULL;
  AppendStr = AllocateZeroPool (0x1000);
  if (AppendStr == NULL)
    return Str->Str;
  VA_START (Args, Fmt);
  UnicodeVSPrint (AppendStr, 0x1000, Fmt, Args);
  VA_END (Args);

  AppendBytes = StrSize (AppendStr);
  UsedBytes = (Str->Str != NULL) ? StrSize (Str->Str) - sizeof (CHAR16) : 0;
  if (UsedBytes > (~(UINTN)0) - AppendBytes) {
    FreePool (AppendStr);
    return Str->Str;
  }
  NeededBytes = UsedBytes + AppendBytes;
  Str->Maxlen = MAX_CHAR;
  if (NeededBytes >= MAX_CHAR * sizeof (CHAR16)) {
    FreePool (AppendStr);
    return Str->Str;
  }

  /*
   * Allocate before freeing the previous string. This makes an allocation
   * failure non-destructive even when firmware reallocation semantics differ.
   * Str->Len counts UTF-16 code units, not allocation bytes.
   */
  NewStr = AllocateZeroPool (NeededBytes);
  if (NewStr == NULL) {
    FreePool (AppendStr);
    return Str->Str;
  }
  if (Str->Str != NULL) {
    CopyMem (NewStr, Str->Str, UsedBytes);
    FreePool (Str->Str);
  }
  Str->Str = NewStr;
  StrCat (Str->Str, AppendStr);
  Str->Len = StrLen (Str->Str);
  FreePool (AppendStr);
  return Str->Str;
}

/*
 * A PCI device-path node supplies device/function numbers relative to its
 * parent bridge. This formatter does not discover the bus or verify a live
 * controller. It appends text to the shared POOL_PRINT buffer.
 */
VOID
DevPathPci (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  PCI_DEVICE_PATH *Pci;

  Pci = DevPath;
  MyCatPrint (Str, L"Pci(%x|%x)", (UINTN) Pci->Device, (UINTN) Pci->Function);
}

VOID
DevPathPccard (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  PCCARD_DEVICE_PATH  *Pccard;

  Pccard = DevPath;
  MyCatPrint (Str, L"Pcmcia(Function%x)", (UINTN) Pccard->FunctionNumber);
}

VOID
DevPathMemMap (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  MEMMAP_DEVICE_PATH  *MemMap;

  MemMap = DevPath;
  MyCatPrint (
    Str,
    L"MemMap(%d:%lx-%lx)",
    (UINTN) MemMap->MemoryType,
    MemMap->StartingAddress,
    MemMap->EndingAddress
    );
}

VOID
DevPathController (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  CONTROLLER_DEVICE_PATH  *Controller;

  Controller = DevPath;
  MyCatPrint (Str, L"Ctrl(%d)", (UINTN) Controller->ControllerNumber);
}

/*
 * Vendor path rendering is descriptive only. The node's vendor GUID selects
 * the textual form, and optional vendor bytes are emitted individually.
 * DevicePathNodeLength is supplied by firmware and must not be mistaken for
 * the length of an independently validated backing allocation.
 */
VOID
DevPathVendor (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  VENDOR_DEVICE_PATH  *Vendor;
  CHAR16              *Type;
  UINTN               DataLength;
  UINTN               Index;

  UINT16              Info;

  Vendor  = DevPath;

  switch (DevicePathType (&Vendor->Header)) {
  case HARDWARE_DEVICE_PATH:
    Type = L"Hw";
    break;

  case MESSAGING_DEVICE_PATH:
    Type = L"Msg";

    if (CompareGuid (&Vendor->Guid, &gEfiSasDevicePathGuid)) {
      MyCatPrint (
        Str,
        L"SAS(%lx,%lx,%x,",
        ((SAS_DEVICE_PATH *) Vendor)->SasAddress,
        ((SAS_DEVICE_PATH *) Vendor)->Lun,
        (UINTN) ((SAS_DEVICE_PATH *) Vendor)->RelativeTargetPort
        );
      Info = (((SAS_DEVICE_PATH *) Vendor)->DeviceTopology);
      if ((Info & 0x0f) == 0) {
        MyCatPrint (Str, L"NoTopology,0,0,0,");
      } else if (((Info & 0x0f) == 1) || ((Info & 0x0f) == 2)) {
        MyCatPrint (
          Str,
          L"%s,%s,%s,",
          ((Info & (0x1 << 4)) != 0) ? L"SATA" : L"SAS",
          ((Info & (0x1 << 5)) != 0) ? L"External" : L"Internal",
          ((Info & (0x1 << 6)) != 0) ? L"Expanded" : L"Direct"
          );
        if ((Info & 0x0f) == 1) {
          MyCatPrint (Str, L"0,");
        } else {
          MyCatPrint (Str, L"%x,", (UINTN) ((Info >> 8) & 0xff));
        }
      } else {
        MyCatPrint (Str, L"0,0,0,0,");
      }

      MyCatPrint (Str, L"%x)", (UINTN) ((SAS_DEVICE_PATH *) Vendor)->Reserved);
      return ;

    } else if (CompareGuid (&Vendor->Guid, &gEfiDebugPortProtocolGuid)) {
      MyCatPrint (Str, L"DebugPort()");
      return ;
    }
    break;

  case MEDIA_DEVICE_PATH:
    Type = L"Media";
    break;

  default:
    Type = L"?";
    break;
  }

  MyCatPrint (Str, L"Ven%s(%g", Type, &Vendor->Guid);
  DataLength = DevicePathNodeLength (&Vendor->Header) - sizeof (VENDOR_DEVICE_PATH);
  if (DataLength > 0) {
    MyCatPrint (Str, L",");
    for (Index = 0; Index < DataLength; Index++) {
      MyCatPrint (Str, L"%02x", (UINTN) ((VENDOR_DEVICE_PATH_WITH_DATA *) Vendor)->VendorDefinedData[Index]);
    }
  }
  MyCatPrint (Str, L")");
}

/*
 * An ACPI hardware ID is displayed as an EISA PNP number where the encoded
 * HID matches that convention, otherwise as a hexadecimal HID. The UID is
 * a firmware path field, not a live ACPI namespace query.
 */
VOID
DevPathAcpi (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  ACPI_HID_DEVICE_PATH  *Acpi;

  Acpi = DevPath;
  if ((Acpi->HID & PNP_EISA_ID_MASK) == PNP_EISA_ID_CONST) {
    MyCatPrint (Str, L"Acpi(PNP%04x,%x)", (UINTN)  EISA_ID_TO_NUM (Acpi->HID), (UINTN) Acpi->UID);
  } else {
    MyCatPrint (Str, L"Acpi(%08x,%x)", (UINTN) Acpi->HID, (UINTN) Acpi->UID);
  }
}

/*
 * Parse the optional HID, UID and CID NUL-delimited suffixes using the node's
 * encoded length. Zero string indices represent absent components; a numeric
 * HID/CID may be rendered in PNP notation. This formatter assumes a complete
 * ACPI node was provided and does not independently verify its enclosing
 * device-path allocation.
 */
VOID
DevPathExtendedAcpi (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  ACPI_EXTENDED_HID_DEVICE_PATH   *ExtendedAcpi;

  UINT16                          HIDSTRIdx;
  UINT16                          UIDSTRIdx;
  UINT16                          CIDSTRIdx;
  UINT16                          Index;
  UINT16                          Length;
  UINT16                          Anchor;
  CHAR8                           *AsChar8Array;

  HIDSTRIdx    = 0;
  UIDSTRIdx    = 0;
  CIDSTRIdx    = 0;
  ExtendedAcpi = DevPath;
  Length       = (UINT16) DevicePathNodeLength ((EFI_DEVICE_PATH_PROTOCOL *) ExtendedAcpi);

  AsChar8Array = (CHAR8 *) ExtendedAcpi;

  Anchor = 16;
  for (Index = Anchor; Index < Length && AsChar8Array[Index] != '\0'; Index++) {
    ;
  }
  if (Index > Anchor) {
    HIDSTRIdx = Anchor;
  }

  Anchor = (UINT16) (Index + 1);
  for (Index = Anchor; Index < Length && AsChar8Array[Index] != '\0'; Index++) {
    ;
  }
  if (Index > Anchor) {
    UIDSTRIdx = Anchor;
  }

  Anchor = (UINT16) (Index + 1);
  for (Index = Anchor; Index < Length && AsChar8Array[Index] != '\0'; Index++) {
    ;
  }
  if (Index > Anchor) {
    CIDSTRIdx = Anchor;
  }

  if (HIDSTRIdx == 0 && CIDSTRIdx == 0 && ExtendedAcpi->UID == 0) {
    MyCatPrint (Str, L"AcpiExp(");
    if ((ExtendedAcpi->HID & PNP_EISA_ID_MASK) == PNP_EISA_ID_CONST) {
      MyCatPrint (Str, L"PNP%04x,", (UINTN) EISA_ID_TO_NUM (ExtendedAcpi->HID));
    } else {
      MyCatPrint (Str, L"%08x,", (UINTN) ExtendedAcpi->HID);
    }
    if ((ExtendedAcpi->CID & PNP_EISA_ID_MASK) == PNP_EISA_ID_CONST) {
      MyCatPrint (Str, L"PNP%04x,", (UINTN)  EISA_ID_TO_NUM (ExtendedAcpi->CID));
    } else {
      MyCatPrint (Str, L"%08x,", (UINTN)  ExtendedAcpi->CID);
    }
    if (UIDSTRIdx != 0) {
      MyCatPrint (Str, L"%a)", AsChar8Array + UIDSTRIdx);
    } else {
      MyCatPrint (Str, L"\"\")");
    }
  } else {
    MyCatPrint (Str, L"AcpiEx(");
    if ((ExtendedAcpi->HID & PNP_EISA_ID_MASK) == PNP_EISA_ID_CONST) {
      MyCatPrint (Str, L"PNP%04x,", (UINTN)  EISA_ID_TO_NUM (ExtendedAcpi->HID));
    } else {
      MyCatPrint (Str, L"%08x,", (UINTN) ExtendedAcpi->HID);
    }
    if ((ExtendedAcpi->CID & PNP_EISA_ID_MASK) == PNP_EISA_ID_CONST) {
      MyCatPrint (Str, L"PNP%04x,", (UINTN) EISA_ID_TO_NUM (ExtendedAcpi->CID));
    } else {
      MyCatPrint (Str, L"%08x,", (UINTN) ExtendedAcpi->CID);
    }
    MyCatPrint (Str, L"%x,", (UINTN) ExtendedAcpi->UID);

    if (HIDSTRIdx != 0) {
      MyCatPrint (Str, L"%a,", AsChar8Array + HIDSTRIdx);
    } else {
      MyCatPrint (Str, L"\"\",");
    }
    if (CIDSTRIdx != 0) {
      MyCatPrint (Str, L"%a,", AsChar8Array + CIDSTRIdx);
    } else {
      MyCatPrint (Str, L"\"\",");
    }
    if (UIDSTRIdx != 0) {
      MyCatPrint (Str, L"%a)", AsChar8Array + UIDSTRIdx);
    } else {
      MyCatPrint (Str, L"\"\")");
    }
  }

}

/*
 * The first ACPI ADR occupies the fixed eight-byte prefix. Subsequent ADR
 * values are 32-bit words counted from the node length. A malformed length
 * below eight would underflow the unsigned count; callers need a validated
 * node before invoking this display-only formatter.
 */
VOID
DevPathAdrAcpi (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  ACPI_ADR_DEVICE_PATH    *AcpiAdr;
  UINT16                  Index;
  UINT16                  Length;
  UINT16                  AdditionalAdrCount;

  AcpiAdr            = DevPath;
  Length             = (UINT16) DevicePathNodeLength ((EFI_DEVICE_PATH_PROTOCOL *) AcpiAdr);
  AdditionalAdrCount = (UINT16) ((Length - 8) / 4);

  MyCatPrint (Str, L"AcpiAdr(%x", (UINTN) AcpiAdr->ADR);
  for (Index = 0; Index < AdditionalAdrCount; Index++) {
    MyCatPrint (Str, L",%x", (UINTN) *(UINT32 *) ((UINT8 *) AcpiAdr + 8 + Index * 4));
  }
  MyCatPrint (Str, L")");
}

VOID
DevPathAtapi (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  ATAPI_DEVICE_PATH *Atapi;

  Atapi = DevPath;
  MyCatPrint (
    Str,
    L"Ata(%s,%s)",
    (Atapi->PrimarySecondary != 0)? L"Secondary" : L"Primary",
    (Atapi->SlaveMaster != 0)? L"Slave" : L"Master"
    );
}

VOID
DevPathScsi (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  SCSI_DEVICE_PATH  *Scsi;

  Scsi = DevPath;
  MyCatPrint (Str, L"Scsi(Pun%x,Lun%x)", (UINTN) Scsi->Pun, (UINTN) Scsi->Lun);
}

VOID
DevPathFibre (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  FIBRECHANNEL_DEVICE_PATH  *Fibre;

  Fibre = DevPath;
  MyCatPrint (Str, L"Fibre(Wwn%lx,Lun%x)", Fibre->WWN, Fibre->Lun);
}

VOID
DevPath1394 (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  F1394_DEVICE_PATH *F1394Path;

  F1394Path = DevPath;
  MyCatPrint (Str, L"1394(%lx)", &F1394Path->Guid);
}

VOID
DevPathUsb (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  USB_DEVICE_PATH *Usb;

  Usb = DevPath;
  MyCatPrint (Str, L"Usb(%x,%x)", (UINTN) Usb->ParentPortNumber, (UINTN) Usb->InterfaceNumber);
}

/*
 * The USB vendor/product/interface fields identify a WWID-type path node,
 * but this formatter prints a literal WWID placeholder rather than the
 * serial-number suffix. Its result is therefore not a unique USB device key.
 */
VOID
DevPathUsbWWID (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  USB_WWID_DEVICE_PATH  *UsbWWId;

  UsbWWId = DevPath;
  MyCatPrint (
    Str,
    L"UsbWwid(%x,%x,%x,\"WWID\")",
    (UINTN) UsbWWId->VendorId,
    (UINTN) UsbWWId->ProductId,
    (UINTN) UsbWWId->InterfaceNumber
    );
}

VOID
DevPathLogicalUnit (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  DEVICE_LOGICAL_UNIT_DEVICE_PATH *LogicalUnit;

  LogicalUnit = DevPath;
  MyCatPrint (Str, L"Unit(%x)", (UINTN) LogicalUnit->Lun);
}

/*
 * A USB class path describes vendor/product IDs and the class, subclass
 * and protocol matching fields. It may match more than one physical USB
 * device and cannot serve as a unique port identity.
 */
VOID
DevPathUsbClass (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  USB_CLASS_DEVICE_PATH *UsbClass;

  UsbClass = DevPath;
  MyCatPrint (
    Str,
    L"Usb Class(%x,%x,%x,%x,%x)",
    (UINTN) UsbClass->VendorId,
    (UINTN) UsbClass->ProductId,
    (UINTN) UsbClass->DeviceClass,
    (UINTN) UsbClass->DeviceSubClass,
    (UINTN) UsbClass->DeviceProtocol
    );
}

/*
 * The direct-connect flag changes the rendering grammar: a direct HBA
 * path omits the port-multiplier component. These are encoded topology
 * values, not proof that firmware currently exposes a usable disk handle.
 */
VOID
DevPathSata (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  SATA_DEVICE_PATH *Sata;

  Sata = DevPath;
  if ((Sata->PortMultiplierPortNumber & SATA_HBA_DIRECT_CONNECT_FLAG) != 0) {
    MyCatPrint (
      Str,
      L"Sata(%x,%x)",
      (UINTN) Sata->HBAPortNumber,
      (UINTN) Sata->Lun
      );
  } else {
    MyCatPrint (
      Str,
      L"Sata(%x,%x,%x)",
      (UINTN) Sata->HBAPortNumber,
      (UINTN) Sata->PortMultiplierPortNumber,
      (UINTN) Sata->Lun
      );
  }
}

VOID
DevPathI2O (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  I2O_DEVICE_PATH *I2OPath;

  I2OPath = DevPath;
  MyCatPrint (Str, L"I2O(%x)", (UINTN) I2OPath->Tid);
}

/*
 * Network interface type decides whether to show a six-byte Ethernet-style
 * address or the full EFI_MAC_ADDRESS field. This prints firmware path
 * bytes and does not resolve active link-layer state.
 */
VOID
DevPathMacAddr (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  MAC_ADDR_DEVICE_PATH  *MACDevPath;
  UINTN                 HwAddressSize;
  UINTN                 Index;

  MACDevPath           = DevPath;

  HwAddressSize = sizeof (EFI_MAC_ADDRESS);
  if (MACDevPath->IfType == 0x01 || MACDevPath->IfType == 0x00) {
    HwAddressSize = 6;
  }

  MyCatPrint (Str, L"Mac(");

  for (Index = 0; Index < HwAddressSize; Index++) {
    MyCatPrint (Str, L"%02x", (UINTN) MACDevPath->MacAddress.Addr[Index]);
  }

  MyCatPrint (Str, L")");
}

/*
 * Print only the remote IPv4 address and port encoded in the messaging node.
 * This is diagnostic text, not network reachability or EFI image validation;
 * the source node's backing length is not independently checked here.
 */
VOID
DevPathIPv4 (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  IPv4_DEVICE_PATH  *IPDevPath;

  IPDevPath = DevPath;
  MyCatPrint (
    Str,
    L"IPv4(%d.%d.%d.%d:%d)",
    (UINTN) IPDevPath->RemoteIpAddress.Addr[0],
    (UINTN) IPDevPath->RemoteIpAddress.Addr[1],
    (UINTN) IPDevPath->RemoteIpAddress.Addr[2],
    (UINTN) IPDevPath->RemoteIpAddress.Addr[3],
    (UINTN) IPDevPath->RemotePort
    );
}

/*
 * Print the remote IPv6 address encoded by firmware. This renderer assumes
 * a complete node; it verifies neither the node's backing buffer length nor
 * the reachability of the displayed endpoint.
 */
VOID
DevPathIPv6 (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  IPv6_DEVICE_PATH  *IPv6DevPath;

  IPv6DevPath = DevPath;
  MyCatPrint (
    Str,
    L"IPv6(%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x)",
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[0],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[1],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[2],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[3],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[4],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[5],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[6],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[7],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[8],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[9],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[10],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[11],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[12],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[13],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[14],
    (UINTN) IPv6DevPath->RemoteIpAddress.Addr[15]
    );
}

VOID
DevPathInfiniBand (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  INFINIBAND_DEVICE_PATH  *InfiniBand;

  InfiniBand = DevPath;
  MyCatPrint (
    Str,
    L"Infiniband(%x,%g,%lx,%lx,%lx)",
    (UINTN) InfiniBand->ResourceFlags,
    InfiniBand->PortGid,
    InfiniBand->ServiceId,
    InfiniBand->TargetPortId,
    InfiniBand->DeviceId
    );
}

/*
 * Zero baud or data-bit values represent protocol defaults rather than
 * literal zero-speed hardware. The encoded parity and stop-bit enumerations
 * are mapped to display tokens; no serial port is opened or configured.
 */
/*
 * Map firmware parity, default baud rate/data bits and stop-bit selectors
 * to a printable UART path. Unrecognised parity/stop-bit values produce an
 * x marker; this does not configure the serial controller.
 */
VOID
DevPathUart (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  UART_DEVICE_PATH  *Uart;
  CHAR8             Parity;

  Uart = DevPath;
  switch (Uart->Parity) {
  case 0:
    Parity = 'D';
    break;

  case 1:
    Parity = 'N';
    break;

  case 2:
    Parity = 'E';
    break;

  case 3:
    Parity = 'O';
    break;

  case 4:
    Parity = 'M';
    break;

  case 5:
    Parity = 'S';
    break;

  default:
    Parity = 'x';
    break;
  }

  if (Uart->BaudRate == 0) {
    MyCatPrint (Str, L"Uart(DEFAULT,%c,", Parity);
  } else {
    MyCatPrint (Str, L"Uart(%ld,%c,", Uart->BaudRate, Parity);
  }

  if (Uart->DataBits == 0) {
    MyCatPrint (Str, L"D,");
  } else {
    MyCatPrint (Str, L"%d,", (UINTN) Uart->DataBits);
  }

  switch (Uart->StopBits) {
  case 0:
    MyCatPrint (Str, L"D)");
    break;

  case 1:
    MyCatPrint (Str, L"1)");
    break;

  case 2:
    MyCatPrint (Str, L"1.5)");
    break;

  case 3:
    MyCatPrint (Str, L"2)");
    break;

  default:
    MyCatPrint (Str, L"x)");
    break;
  }
}

/*
 * The iSCSI option word encodes digest modes and authentication choices.
 * Displaying the requested settings does not establish that a remote target
 * was reached or that an authenticated session took place.
 */
VOID
DevPathiSCSI (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  ISCSI_DEVICE_PATH_WITH_NAME *IScsi;
  UINT16                      Options;

  IScsi = DevPath;
  MyCatPrint (
    Str,
    L"iSCSI(%a,%x,%lx,",
    IScsi->TargetName,
    (UINTN) IScsi->TargetPortalGroupTag,
    IScsi->Lun
    );

  Options = IScsi->LoginOption;
  MyCatPrint (Str, L"%s,", (((Options >> 1) & 0x0001) != 0) ? L"CRC32C" : L"None");
  MyCatPrint (Str, L"%s,", (((Options >> 3) & 0x0001) != 0) ? L"CRC32C" : L"None");
  if (((Options >> 11) & 0x0001) != 0) {
    MyCatPrint (Str, L"%s,", L"None");
  } else if (((Options >> 12) & 0x0001) != 0) {
    MyCatPrint (Str, L"%s,", L"CHAP_UNI");
  } else {
    MyCatPrint (Str, L"%s,", L"CHAP_BI");

  }

  MyCatPrint (Str, L"%s)", (IScsi->NetworkProtocol == 0) ? L"TCP" : L"reserved");
}

/*
 * Append the encoded VLAN identifier from a network messaging node. The
 * displayed value does not establish that the physical NIC or network
 * stack has joined that VLAN.
 */
VOID
DevPathVlan (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  VLAN_DEVICE_PATH  *Vlan;

  Vlan = DevPath;
  MyCatPrint (Str, L"Vlan(%d)", (UINTN) Vlan->VlanId);
}

/*
 * Format the partition number and embedded MBR signature or GPT GUID.
 * The source must contain a complete binary node. This representation does
 * not verify the current partition table, media access or image integrity.
 */
VOID
DevPathHardDrive (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  HARDDRIVE_DEVICE_PATH *Hd;

  Hd = DevPath;
  switch (Hd->SignatureType) {
  case SIGNATURE_TYPE_MBR:
    MyCatPrint (
      Str,
      L"HD(Part%d,Sig%08x)",
      (UINTN) Hd->PartitionNumber,
      (UINTN) *((UINT32 *) (&(Hd->Signature[0])))
      );
    break;

  case SIGNATURE_TYPE_GUID:
    MyCatPrint (
      Str,
      L"HD(Part%d,Sig%g)",
      (UINTN) Hd->PartitionNumber,
      (EFI_GUID *) &(Hd->Signature[0])
      );
    break;

  default:
    MyCatPrint (
      Str,
      L"HD(Part%d,MBRType=%02x,SigType=%02x)",
      (UINTN) Hd->PartitionNumber,
      (UINTN) Hd->MBRType,
      (UINTN) Hd->SignatureType
      );
    break;
  }
}

VOID
DevPathCDROM (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  CDROM_DEVICE_PATH *Cd;

  Cd = DevPath;
  MyCatPrint (Str, L"CDROM(Entry%x)", (UINTN) Cd->BootEntry);
}

/*
 * Formats the FILEPATH_DEVICE_PATH node into text. The formatted Unicode
 * path must not be treated as an authenticated EFI application.
 */
VOID
DevPathFilePath (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  FILEPATH_DEVICE_PATH  *Fp;

  Fp = DevPath;
  MyCatPrint (Str, L"%s", Fp->PathName);
}

/*
 * Render the media protocol GUID carried by this node. The formatter does
 * not locate the named protocol on any current firmware handle.
 */
VOID
DevPathMediaProtocol (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  MEDIA_PROTOCOL_DEVICE_PATH  *MediaProt;

  MediaProt = DevPath;
  MyCatPrint (Str, L"Media(%g)", &MediaProt->Protocol);
}

VOID
DevPathFvFilePath (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *FvFilePath;

  FvFilePath = DevPath;
  MyCatPrint (Str, L"%g", &FvFilePath->FvFileName);
}

/*
 * Print the firmware-supplied starting and ending offsets. This routine
 * does not establish the existence, ordering or accessibility of that range.
 */
VOID
MyDevPathRelativeOffsetRange (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  MEDIA_RELATIVE_OFFSET_RANGE_DEVICE_PATH *Offset;

  Offset = DevPath;
  MyCatPrint (
    Str,
    L"Offset(%lx,%lx)",
    Offset->StartingOffset,
    Offset->EndingOffset
    );
}

/*
 * Legacy BBS device types are translated to descriptive labels only.
 * A BBS node cannot establish that the machine has CSM support or that
 * the selected legacy device can actually start.
 */
VOID
DevPathBssBss (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  BBS_BBS_DEVICE_PATH *Bbs;
  CHAR16              *Type;

  Bbs = DevPath;
  switch (Bbs->DeviceType) {
  case BBS_TYPE_FLOPPY:
    Type = L"Floppy";
    break;

  case BBS_TYPE_HARDDRIVE:
    Type = L"Harddrive";
    break;

  case BBS_TYPE_CDROM:
    Type = L"CDROM";
    break;

  case BBS_TYPE_PCMCIA:
    Type = L"PCMCIA";
    break;

  case BBS_TYPE_USB:
    Type = L"Usb";
    break;

  case BBS_TYPE_EMBEDDED_NETWORK:
    Type = L"Net";
    break;

  case BBS_TYPE_BEV:
    Type = L"BEV";
    break;

  default:
    Type = L"?";
    break;
  }
  MyCatPrint (Str, L"Legacy-%s", Type);
}

/*
 * A comma separates complete device-path instances, whereas slash
 * separators belong between nodes in one instance. The caller must not
 * insert a second separator around this terminator.
 */
VOID
DevPathEndInstance (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  MyCatPrint (Str, L",");
}

/*
 * Unknown type/subtype pairs intentionally render as '?' instead of being
 * classified as valid hardware. This fallback has no side effect on the
 * original firmware device path or on boot selection.
 */
VOID
DevPathNodeUnknown (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  MyCatPrint (Str, L"?");
}

/*
 * Render the firmware-volume GUID as a descriptive device-path element. A
 * GUID string is not proof that firmware can open or authenticate a file
 * in that volume.
 */
VOID
DevPathFvPath (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath
  )
{
  MEDIA_FW_VOL_DEVICE_PATH *FvPath;

  FvPath = DevPath;
  MyCatPrint (Str, L"Fv(%g)", &FvPath->FvName);
}

/*
 * Mapping from UEFI type/subtype pairs to textual renderers. This table
 * defines formatting coverage, not an allow-list of supported boot sources:
 * an unrecognised node is rendered by DevPathNodeUnknown instead.
 */
DEVICE_PATH_STRING_TABLE  DevPathTable[] = {
  {
    HARDWARE_DEVICE_PATH,
    HW_PCI_DP,
    DevPathPci
  },
  {
    HARDWARE_DEVICE_PATH,
    HW_PCCARD_DP,
    DevPathPccard
  },
  {
    HARDWARE_DEVICE_PATH,
    HW_MEMMAP_DP,
    DevPathMemMap
  },
  {
    HARDWARE_DEVICE_PATH,
    HW_VENDOR_DP,
    DevPathVendor
  },
  {
    HARDWARE_DEVICE_PATH,
    HW_CONTROLLER_DP,
    DevPathController
  },
  {
    ACPI_DEVICE_PATH,
    ACPI_DP,
    DevPathAcpi
  },
  {
    ACPI_DEVICE_PATH,
    ACPI_EXTENDED_DP,
    DevPathExtendedAcpi
  },
  {
    ACPI_DEVICE_PATH,
    ACPI_ADR_DP,
    DevPathAdrAcpi
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_ATAPI_DP,
    DevPathAtapi
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_SCSI_DP,
    DevPathScsi
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_FIBRECHANNEL_DP,
    DevPathFibre
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_1394_DP,
    DevPath1394
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_USB_DP,
    DevPathUsb
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_USB_WWID_DP,
    DevPathUsbWWID
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_DEVICE_LOGICAL_UNIT_DP,
    DevPathLogicalUnit
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_USB_CLASS_DP,
    DevPathUsbClass
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_SATA_DP,
    DevPathSata
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_I2O_DP,
    DevPathI2O
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_MAC_ADDR_DP,
    DevPathMacAddr
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_IPv4_DP,
    DevPathIPv4
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_IPv6_DP,
    DevPathIPv6
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_INFINIBAND_DP,
    DevPathInfiniBand
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_UART_DP,
    DevPathUart
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_VENDOR_DP,
    DevPathVendor
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_ISCSI_DP,
    DevPathiSCSI
  },
  {
    MESSAGING_DEVICE_PATH,
    MSG_VLAN_DP,
    DevPathVlan
  },
  {
    MEDIA_DEVICE_PATH,
    MEDIA_HARDDRIVE_DP,
    DevPathHardDrive
  },
  {
    MEDIA_DEVICE_PATH,
    MEDIA_CDROM_DP,
    DevPathCDROM
  },
  {
    MEDIA_DEVICE_PATH,
    MEDIA_VENDOR_DP,
    DevPathVendor
  },
  {
    MEDIA_DEVICE_PATH,
    MEDIA_FILEPATH_DP,
    DevPathFilePath
  },
  {
    MEDIA_DEVICE_PATH,
    MEDIA_PROTOCOL_DP,
    DevPathMediaProtocol
  },
  {
    MEDIA_DEVICE_PATH,
    MEDIA_PIWG_FW_VOL_DP,
    DevPathFvPath,
  },
  {
    MEDIA_DEVICE_PATH,
    MEDIA_PIWG_FW_FILE_DP,
    DevPathFvFilePath
  },
  {
    MEDIA_DEVICE_PATH,
    MEDIA_RELATIVE_OFFSET_RANGE_DP,
    MyDevPathRelativeOffsetRange,
  },
  {
    BBS_DEVICE_PATH,
    BBS_BBS_DP,
    DevPathBssBss
  },
  {
    END_DEVICE_PATH_TYPE,
    END_INSTANCE_DEVICE_PATH_SUBTYPE,
    DevPathEndInstance
  },
  {
    0,
    0,
    NULL
  }
};

/**
 * Prefer the firmware's DevicePathToText protocol. The local formatter walks
 * nodes by encoded length and picks a printer by type and subtype. Its separators
 * distinguish nodes within a path from instance boundaries. The resulting CHAR16
 * buffer is allocated for the caller. The fallback has no independently
 * supplied total path length: it assumes node lengths eventually reach a
 * valid terminator rather than establishing binary path well-formedness.
 * MyCatPrint now preserves its prior buffer on pool exhaustion, counts CHAR16
 * units correctly and stops appending at MAX_CHAR. Truncation can therefore
 * produce an incomplete diagnostic string, never a validated boot path.
 */
CHAR16 *
EFIAPI
DevicePathToStr (
  IN EFI_DEVICE_PATH_PROTOCOL     *DevPath
  )
{
  POOL_PRINT                Str;
  EFI_DEVICE_PATH_PROTOCOL  *DevPathNode;
  VOID (*DumpNode) (POOL_PRINT *, VOID *);

  UINTN Index;

  EFI_STATUS                       Status;
  CHAR16                           *ToText;
  EFI_DEVICE_PATH_TO_TEXT_PROTOCOL *DevPathToText;

  ZeroMem (&Str, sizeof (Str));

  if (DevPath == NULL) {
    goto Done;
  }

  Status = gBS->LocateProtocol (
                  &gEfiDevicePathToTextProtocolGuid,
                  NULL,
                  (VOID **) &DevPathToText
                  );
  if (!EFI_ERROR (Status)) {
    ToText = DevPathToText->ConvertDevicePathToText (
                              DevPath,
                              FALSE,
                              TRUE
                              );
    ASSERT (ToText != NULL);
    return ToText;
  }

  DevPathNode = DevPath;
  while (!IsDevicePathEnd (DevPathNode)) {

    DumpNode = NULL;
    for (Index = 0; DevPathTable[Index].Function != NULL; Index += 1) {

      if (DevicePathType (DevPathNode) == DevPathTable[Index].Type &&
          DevicePathSubType (DevPathNode) == DevPathTable[Index].SubType
          ) {
        DumpNode = DevPathTable[Index].Function;
        break;
      }
    }

    if (!DumpNode) {
      DumpNode = DevPathNodeUnknown;
    }

    /*
     * Each renderer contributes one node to the textual path. The
     * end-of-instance renderer supplies its own separator, so adding a slash
     * before it would change the grammar of a multi-instance path.
     */
    if ((Str.Len != 0) && (DumpNode != DevPathEndInstance)) {
      MyCatPrint (&Str, L"/");
    }

    DumpNode (&Str, DevPathNode);

    DevPathNode = NextDevicePathNode (DevPathNode);
  }

Done:
  /* MyCatPrint maintains the terminator and counts UTF-16 code units. An
   * empty path still needs a caller-owned, terminated string. */
  if (Str.Str == NULL)
    Str.Str = AllocateZeroPool (sizeof (CHAR16));
  return Str.Str;
}
