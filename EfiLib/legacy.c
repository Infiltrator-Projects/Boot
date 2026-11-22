/*
 * Legacy boot-option discovery and EFI BDS variable management. BIOS/CSM
 * support is firmware-specific; legacy entries and device ordering can alter
 * boot variables and must not be confused with non-destructive UEFI scanning.
 * Device-path and BBS indexes are firmware-generated and may be reordered;
 * mapping tables are snapshots, not durable identities. Variable writes and
 * legacy handoff need separate success/failure interpretation.
 */

#ifdef __MAKEWITH_GNUEFI
#include "efi.h"
#include "efilib.h"
#include "gnuefi-helper.h"
#define EFI_DEVICE_PATH_PROTOCOL EFI_DEVICE_PATH
#define EfiReallocatePool ReallocatePool
#define EfiLibLocateProtocol LibLocateProtocol
#else
#include "../include/tiano_includes.h"
#endif
#include "legacy.h"
#include "GenericBdsLib.h"
#include "../boot/global.h"
#include "../boot/lib.h"
#include "../include/refit_call_wrapper.h"

BOOT_OPTION_BBS_MAPPING  *mBootOptionBbsMapping     = NULL;
UINTN                    mBootOptionBbsMappingCount = 0;

extern EFI_DEVICE_PATH EndDevicePath[];
extern EFI_GUID gEfiLegacyBiosProtocolGuid;
EFI_GUID gEfiLegacyDevOrderVariableGuid     = { 0xa56074db, 0x65fe, 0x45f7, {0xbd, 0x21, 0x2d, 0x2b, 0xdd, 0x8e, 0x96, 0x52 }};
static EFI_GUID EfiGlobalVariableGuid = { 0x8BE4DF61, 0x93CA, 0x11D2, { 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C }};

/*
 * Convert a terminated single-byte string into UTF-16 code units. KNOWN
 * LIMIT: the size comparison occurs after a source byte has already been
 * read and written, and the terminator is written at the final index. The
 * caller must provide appropriately terminated source data and sufficient
 * destination capacity; this routine does not independently prove those
 * bounds.
 */
VOID
AsciiToUnicodeSize (
  IN UINT8              *AStr,
  IN UINTN              Size,
  OUT UINT16            *UStr
  )
{
  UINTN Idx;

  Idx = 0;
  while (AStr[Idx] != 0) {
    UStr[Idx] = (CHAR16) AStr[Idx];
    if (Idx == Size) {
      break;
    }

    Idx++;
  }
  UStr[Idx] = 0;
}

VOID
BdsBuildLegacyDevNameString (
  IN  BBS_TABLE                 *CurBBSEntry,
  IN  UINTN                     Index,
  IN  UINTN                     BufSize,
  OUT CHAR16                    *BootString
  )
{
  CHAR16  *Fmt;
  CHAR16  *Type;
  UINT8   *StringDesc;
  CHAR16  Temp[80];

  switch (Index) {

  case 1:
    Fmt = L"Primary Master %s";
    break;

  case 2:
    Fmt = L"Primary Slave %s";
    break;

  case 3:
    Fmt = L"Secondary Master %s";
    break;

  case 4:
    Fmt = L"Secondary Slave %s";
    break;

  default:
    Fmt = L"%s";
    break;
  }

  switch (CurBBSEntry->DeviceType) {
  case BBS_FLOPPY:
    Type = L"Floppy";
    break;

  case BBS_HARDDISK:
    Type = L"Harddisk";
    break;

  case BBS_CDROM:
    Type = L"CDROM";
    break;

  case BBS_PCMCIA:
    Type = L"PCMCIAe";
    break;

  case BBS_USB:
    Type = L"USB";
    break;

  case BBS_EMBED_NETWORK:
    Type = L"Network";
    break;

  case BBS_BEV_DEVICE:
    Type = L"BEVe";
    break;

  case BBS_UNKNOWN:
  default:
    Type = L"Unknown";
    break;
  }

  StringDesc = (UINT8 *) (UINTN) ((CurBBSEntry->DescStringSegment << 4) + CurBBSEntry->DescStringOffset);
  if (NULL != StringDesc) {

    AsciiToUnicodeSize (StringDesc, 32, Temp);
    Fmt   = L"%s";
    Type  = Temp;
  }

  if (Index >= 5 && Index <= 16 && (CurBBSEntry->DeviceType == BBS_HARDDISK || CurBBSEntry->DeviceType == BBS_CDROM)) {
    Fmt = L"%s %d";
    SPrint(BootString, BufSize, Fmt, Type, Index - 5);
  } else {
    SPrint(BootString, BufSize, Fmt, Type);
  }
}

/**
 * Step over the attributes, 16-bit device-path byte length and terminated CHAR16
 * description. For a BBS path the following optional data holds a BBS table
 * record and a 16-bit index. The returned record aliases the original variable,
 * whose lifetime and record bounds are the caller's responsibility.
 */
/*
 * Inspects an EFI load option to classify legacy boot encoding.
 * Classification alone does not prove CSM execution support.
 */
BOOLEAN
BdsIsLegacyBootOption (
  IN UINT8                 *BootOptionVar,
  OUT BBS_TABLE            **BbsEntry,
  OUT UINT16               *BbsIndex
  )
{
  UINT8                     *Ptr;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  BOOLEAN                   Ret;
  UINT16                    DevPathLen;

  Ptr = BootOptionVar;
  Ptr += sizeof (UINT32);
  DevPathLen = *(UINT16 *) Ptr;
  Ptr += sizeof (UINT16);
  Ptr += StrSize ((UINT16 *) Ptr);
  DevicePath = (EFI_DEVICE_PATH_PROTOCOL *) Ptr;
  if ((BBS_DEVICE_PATH == DevicePath->Type) && (BBS_BBS_DP == DevicePath->SubType)) {
    Ptr += DevPathLen;
    *BbsEntry = (BBS_TABLE *) Ptr;
    Ptr += sizeof (BBS_TABLE);
    *BbsIndex = *(UINT16 *) Ptr;
    Ret       = TRUE;
  } else {
    *BbsEntry = NULL;
    Ret       = FALSE;
  }

  return Ret;
}

BOOLEAN
BdsFindLegacyBootOptionByDevTypeAndName (
  IN UINT16                 *BootOrder,
  IN UINTN                  BootOptionNum,
  IN UINT16                 DevType,
  IN CHAR16                 *DevName,
  OUT UINT32                *Attribute,
  OUT UINT16                *BbsIndex,
  OUT UINT16                *OptionNumber
  )
{
  UINTN     Index;
  CHAR16    BootOption[10];
  UINTN     BootOptionSize;
  UINT8     *BootOptionVar;
  BBS_TABLE *BbsEntry;
  BOOLEAN   Found;

  BbsEntry  = NULL;
  Found     = FALSE;

  if (NULL == BootOrder) {
    return Found;
  }

  for (Index = 0; Index < BootOptionNum; Index++) {
    SPrint(BootOption, sizeof (BootOption), L"Boot%04x", (UINTN) BootOrder[Index]);
    BootOptionVar = BdsLibGetVariableAndSize (
                      BootOption,
                      &EfiGlobalVariableGuid,
                      &BootOptionSize
                      );
    if (NULL == BootOptionVar) {
       continue;
    }

    if (!BdsIsLegacyBootOption (BootOptionVar, &BbsEntry, BbsIndex)) {
      MyFreePool (BootOptionVar);
      continue;
    }

    if (
        (BbsEntry->DeviceType != DevType) ||
        (StrCmp (DevName, (CHAR16*)(BootOptionVar + sizeof (UINT32) + sizeof (UINT16))) != 0)
       ) {
      MyFreePool (BootOptionVar);
      continue;
    }

    *Attribute    = *(UINT32 *) BootOptionVar;
    *OptionNumber = BootOrder[Index];
    Found         = TRUE;
    MyFreePool (BootOptionVar);
    break;
  }

  return Found;
}

/*
 * Constructs a legacy boot option from BBS state and its device path.
 * The output participates in firmware boot-order bookkeeping; a created
 * descriptor does not certify that a BIOS compatibility handoff will work.
 */
EFI_STATUS
BdsCreateLegacyBootOption (
  IN BBS_TABLE                        *CurrentBbsEntry,
  IN EFI_DEVICE_PATH_PROTOCOL         *CurrentBbsDevPath,
  IN UINTN                            Index,
  IN OUT UINT16                       **BootOrderList,
  IN OUT UINTN                        *BootOrderListSize
  )
{
  EFI_STATUS           Status;
  UINT16               CurrentBootOptionNo;
  UINT16               BootString[10];
  CHAR16               BootDesc[100];
  CHAR8                HelpString[100];
  UINT16               *NewBootOrderList;
  UINTN                BufferSize;
  UINTN                StringLen;
  VOID                 *Buffer;
  UINT8                *Ptr;
  UINT16               CurrentBbsDevPathSize;
  UINTN                BootOrderIndex;
  UINTN                BootOrderLastIndex;
  UINTN                ArrayIndex;
  BOOLEAN              IndexNotFound;
  BBS_BBS_DEVICE_PATH  *NewBbsDevPathNode;

  if ((*BootOrderList) == NULL) {
    CurrentBootOptionNo = 0;
  } else {
    for (ArrayIndex = 0; ArrayIndex < (UINTN) (*BootOrderListSize / sizeof (UINT16)); ArrayIndex++) {
      IndexNotFound = TRUE;
      for (BootOrderIndex = 0; BootOrderIndex < (UINTN) (*BootOrderListSize / sizeof (UINT16)); BootOrderIndex++) {
        if ((*BootOrderList)[BootOrderIndex] == ArrayIndex) {
          IndexNotFound = FALSE;
          break;
        }
      }

      if (!IndexNotFound) {
        continue;
      } else {
        break;
      }
    }

    CurrentBootOptionNo = (UINT16) ArrayIndex;
  }

  SPrint (
    BootString,
    sizeof (BootString),
    L"Boot%04x",
    CurrentBootOptionNo
    );

  BdsBuildLegacyDevNameString (CurrentBbsEntry, Index, sizeof (BootDesc), BootDesc);

  UnicodeStrToAsciiStr (BootDesc, HelpString);

  StringLen = AsciiStrLen (HelpString);
  NewBbsDevPathNode = AllocateZeroPool (sizeof (BBS_BBS_DEVICE_PATH) + StringLen);
  if (NewBbsDevPathNode == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem (NewBbsDevPathNode, CurrentBbsDevPath, sizeof (BBS_BBS_DEVICE_PATH));
  CopyMem (NewBbsDevPathNode->String, HelpString, StringLen + 1);
  SetDevicePathNodeLength (&(NewBbsDevPathNode->Header), sizeof (BBS_BBS_DEVICE_PATH) + StringLen);

  CurrentBbsDevPath = AppendDevicePathNode (
                        EndDevicePath,
                        (EFI_DEVICE_PATH_PROTOCOL *) NewBbsDevPathNode
                        );
   if (CurrentBbsDevPath == NULL) {
    MyFreePool (NewBbsDevPathNode);
    return EFI_OUT_OF_RESOURCES;
  }

  CurrentBbsDevPathSize = (UINT16) (GetDevicePathSize (CurrentBbsDevPath));

  BufferSize = sizeof (UINT32) +
    sizeof (UINT16) +
    StrSize (BootDesc) +
    CurrentBbsDevPathSize +
    sizeof (BBS_TABLE) +
    sizeof (UINT16);

  Buffer = AllocateZeroPool (BufferSize);
  if (Buffer == NULL) {
    MyFreePool (NewBbsDevPathNode);
    MyFreePool (CurrentBbsDevPath);
    return EFI_OUT_OF_RESOURCES;
  }

  Ptr               = (UINT8 *) Buffer;

  *((UINT32 *) Ptr) = LOAD_OPTION_ACTIVE;
  Ptr += sizeof (UINT32);

  *((UINT16 *) Ptr) = CurrentBbsDevPathSize;
  Ptr += sizeof (UINT16);

  CopyMem (
    Ptr,
    BootDesc,
    StrSize (BootDesc)
    );
  Ptr += StrSize (BootDesc);

  CopyMem (
    Ptr,
    CurrentBbsDevPath,
    CurrentBbsDevPathSize
    );
  Ptr += CurrentBbsDevPathSize;

  CopyMem (
    Ptr,
    CurrentBbsEntry,
    sizeof (BBS_TABLE)
    );

  Ptr += sizeof (BBS_TABLE);
  *((UINT16 *) Ptr) = (UINT16) Index;
  Status = EfivarSetRaw(&EfiGlobalVariableGuid, BootString,
                        (CHAR8*) Buffer,
                        BufferSize, TRUE);
  MyFreePool (Buffer);

  Buffer = NULL;

  NewBootOrderList = AllocateZeroPool (*BootOrderListSize + sizeof (UINT16));
  if (NULL == NewBootOrderList) {
    MyFreePool (NewBbsDevPathNode);
    MyFreePool (CurrentBbsDevPath);
    return EFI_OUT_OF_RESOURCES;
  }

  if (*BootOrderList != NULL) {
    CopyMem (NewBootOrderList, *BootOrderList, *BootOrderListSize);
    MyFreePool (*BootOrderList);
  }

  BootOrderLastIndex                    = (UINTN) (*BootOrderListSize / sizeof (UINT16));
  NewBootOrderList[BootOrderLastIndex]  = CurrentBootOptionNo;
  *BootOrderListSize += sizeof (UINT16);
  *BootOrderList = NewBootOrderList;

  MyFreePool (NewBbsDevPathNode);
  MyFreePool (CurrentBbsDevPath);
  return Status;
}

EFI_STATUS
BdsCreateOneLegacyBootOption (
  IN BBS_TABLE              *BbsItem,
  IN UINTN                  Index,
  IN OUT UINT16             **BootOrderList,
  IN OUT UINTN              *BootOrderListSize
  )
{
  BBS_BBS_DEVICE_PATH       BbsDevPathNode;
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  *DevPath;

  DevPath                       = NULL;

  BbsDevPathNode.Header.Type    = BBS_DEVICE_PATH;
  BbsDevPathNode.Header.SubType = BBS_BBS_DP;
  SetDevicePathNodeLength (&BbsDevPathNode.Header, sizeof (BBS_BBS_DEVICE_PATH));
  BbsDevPathNode.DeviceType = BbsItem->DeviceType;
  CopyMem (&BbsDevPathNode.StatusFlag, &BbsItem->StatusFlags, sizeof (UINT16));

  DevPath = AppendDevicePathNode (
              EndDevicePath,
              (EFI_DEVICE_PATH_PROTOCOL *) &BbsDevPathNode
              );
  if (NULL == DevPath) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = BdsCreateLegacyBootOption (
            BbsItem,
            DevPath,
            Index,
            BootOrderList,
            BootOrderListSize
            );
  BbsItem->BootPriority = 0x00;

  MyFreePool (DevPath);

  return Status;
}

/**
 * Maintain the next insertion index for each BBS type already encountered. Move
 * later options next to their group, shift the intervening options one slot, then
 * update insertion indices affected by the shift. The order within each group
 * follows the original BootOrder array.
 */
VOID
GroupMultipleLegacyBootOption4SameType (
  UINT16                   *BootOption,
  UINTN                    BootOptionCount
  )
{
  UINTN                    DeviceTypeIndex[7];
  UINTN                    Index;
  UINTN                    MappingIndex;
  UINTN                    *NextIndex;
  UINT16                   OptionNumber;
  UINTN                    DeviceIndex;

  SetMem (DeviceTypeIndex, sizeof (DeviceTypeIndex), 0xFF);

  for (Index = 0; Index < BootOptionCount; Index++) {

    for (MappingIndex = 0; MappingIndex < mBootOptionBbsMappingCount; MappingIndex++) {
      if (mBootOptionBbsMapping[MappingIndex].BootOptionNumber == BootOption[Index]) {
        break;
      }
    }
    if (MappingIndex == mBootOptionBbsMappingCount) {

      continue;
    }

    ASSERT ((mBootOptionBbsMapping[MappingIndex].BbsType & 0xF) < 
             sizeof (DeviceTypeIndex) / sizeof (DeviceTypeIndex[0]));
    /*
     * Each type remembers its next insertion slot in BootOption. Moving a
     * later option into its group shifts all intervening entries, including
     * other groups' saved slots, which the following loop adjusts.
     */
    NextIndex = &DeviceTypeIndex[mBootOptionBbsMapping[MappingIndex].BbsType & 0xF];
    if (*NextIndex == (UINTN) -1) {

      *NextIndex = Index + 1;
    } else {

      OptionNumber = BootOption[Index];
      CopyMem (&BootOption[*NextIndex + 1], &BootOption[*NextIndex], (Index - *NextIndex) * sizeof (UINT16));
      BootOption[*NextIndex] = OptionNumber;

      for (DeviceIndex = 0; DeviceIndex < sizeof (DeviceTypeIndex) / sizeof (DeviceTypeIndex[0]); DeviceIndex++) {
        if (DeviceTypeIndex[DeviceIndex] != (UINTN) -1 && DeviceTypeIndex[DeviceIndex] >= *NextIndex) {
          DeviceTypeIndex[DeviceIndex]++;
        }
      }
    }
  }
}

VOID *
EfiLibGetVariable (
  IN CHAR16               *Name,
  IN EFI_GUID             *VendorGuid
  )
{
  UINTN VarSize;

  return BdsLibGetVariableAndSize (Name, VendorGuid, &VarSize);
}

EFI_STATUS
EfiLibDeleteVariable (
  IN CHAR16   *VarName,
  IN EFI_GUID *VarGuid
  )
{
  VOID        *VarBuf;
  EFI_STATUS  Status;

  VarBuf  = EfiLibGetVariable (VarName, VarGuid);
  Status  = EFI_NOT_FOUND;

  if (VarBuf != NULL) {

    Status = refit_call5_wrapper(gRT->SetVariable, VarName, VarGuid, VAR_FLAG, 0, NULL);
    ASSERT (!EFI_ERROR (Status));
    MyFreePool (VarBuf);
  }

  return Status;
}

/*
 * May synthesise firmware legacy entries from available BBS devices; avoiding
 * unintended duplicates requires exact device identity, not only labels.
 */
EFI_STATUS
BdsAddNonExistingLegacyBootOptions (
  VOID
  )
{
  UINT16                    *BootOrder;
  UINTN                     BootOrderSize;
  EFI_STATUS                Status;
  CHAR16                    Desc[100];
  UINT16                    HddCount;
  UINT16                    BbsCount;
  HDD_INFO                  *LocalHddInfo;
  BBS_TABLE                 *LocalBbsTable;
  UINT16                    BbsIndex;
  EFI_LEGACY_BIOS_PROTOCOL  *LegacyBios;
  UINT16                    Index;
  UINT32                    Attribute;
  UINT16                    OptionNumber;
  BOOLEAN                   Exist;

  HddCount      = 0;
  BbsCount      = 0;
  LocalHddInfo  = NULL;
  LocalBbsTable = NULL;

  Status        = EfiLibLocateProtocol (&gEfiLegacyBiosProtocolGuid, (VOID **) &LegacyBios);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (mBootOptionBbsMapping != NULL) {
    MyFreePool (mBootOptionBbsMapping);

    mBootOptionBbsMapping      = NULL;
    mBootOptionBbsMappingCount = 0;
  }

  refit_call5_wrapper(LegacyBios->GetBbsInfo,
                LegacyBios,
                &HddCount,
                &LocalHddInfo,
                &BbsCount,
                &LocalBbsTable
                );

  BootOrder = BdsLibGetVariableAndSize (
                L"BootOrder",
                &EfiGlobalVariableGuid,
                &BootOrderSize
                );
  if (BootOrder == NULL) {
    BootOrderSize = 0;
  }

  for (Index = 0; Index < BbsCount; Index++) {
    if ((LocalBbsTable[Index].BootPriority == BBS_IGNORE_ENTRY) ||
        (LocalBbsTable[Index].BootPriority == BBS_DO_NOT_BOOT_FROM)
        ) {
      continue;
    }

    BdsBuildLegacyDevNameString (&LocalBbsTable[Index], Index, sizeof (Desc), Desc);

    Exist = BdsFindLegacyBootOptionByDevTypeAndName (
              BootOrder,
              BootOrderSize / sizeof (UINT16),
              LocalBbsTable[Index].DeviceType,
              Desc,
              &Attribute,
              &BbsIndex,
              &OptionNumber
              );
    if (!Exist) {

      Status = BdsCreateOneLegacyBootOption (
                &LocalBbsTable[Index],
                Index,
                &BootOrder,
                &BootOrderSize
                );
      if (EFI_ERROR (Status)) {
        break;
      }
      BbsIndex     = Index;
      OptionNumber = BootOrder[BootOrderSize / sizeof (UINT16) - 1];
    }

    ASSERT (BbsIndex == Index);

    mBootOptionBbsMapping = EfiReallocatePool (
                              mBootOptionBbsMapping,
                              mBootOptionBbsMappingCount * sizeof (BOOT_OPTION_BBS_MAPPING),
                              (mBootOptionBbsMappingCount + 1) * sizeof (BOOT_OPTION_BBS_MAPPING)
                              );
    ASSERT (mBootOptionBbsMapping != NULL);
    mBootOptionBbsMapping[mBootOptionBbsMappingCount].BootOptionNumber = OptionNumber;
    mBootOptionBbsMapping[mBootOptionBbsMappingCount].BbsIndex         = Index;
    mBootOptionBbsMapping[mBootOptionBbsMappingCount].BbsType          = LocalBbsTable[Index].DeviceType;
    mBootOptionBbsMappingCount ++;
  }

  GroupMultipleLegacyBootOption4SameType (
    BootOrder,
    BootOrderSize / sizeof (UINT16)
    );

  if (BootOrderSize > 0) {
    Status = EfivarSetRaw(&EfiGlobalVariableGuid, L"BootOrder",
                          (CHAR8*) BootOrder,
                          BootOrderSize, TRUE);
  } else {
    EfiLibDeleteVariable (L"BootOrder", &EfiGlobalVariableGuid);
  }

  if (BootOrder != NULL) {
    MyFreePool (BootOrder);
  }

  return Status;
}

/*
 * Removes a selected firmware boot option from the managed order data.
 * This concerns persistent boot configuration, not the display-only
 * hiding of a menu entry; callers must propagate firmware write failures.
 * This implementation deletes Boot#### before removing the number from its
 * in-memory BootOrder array; failures may leave order and option out of sync.
 * A successful result here is not an atomic firmware-variable transaction.
 */
EFI_STATUS
BdsDeleteBootOption (
  IN UINTN                       OptionNumber,
  IN OUT UINT16                  *BootOrder,
  IN OUT UINTN                   *BootOrderSize
  )
{
  UINT16      BootOption[100];
  UINTN       Index;
  EFI_STATUS  Status;
  UINTN       Index2Del;

  Status    = EFI_SUCCESS;
  Index2Del = 0;

  SPrint (BootOption, sizeof (BootOption), L"Boot%04x", OptionNumber);
  Status = EfiLibDeleteVariable (BootOption, &EfiGlobalVariableGuid);

  for (Index = 0; Index < *BootOrderSize / sizeof (UINT16); Index++) {
    if (BootOrder[Index] == OptionNumber) {
      Index2Del = Index;
      break;
    }
  }

  if (Index != *BootOrderSize / sizeof (UINT16)) {
    for (Index = 0; Index < *BootOrderSize / sizeof (UINT16) - 1; Index++) {
      if (Index >= Index2Del) {
        BootOrder[Index] = BootOrder[Index + 1];
      }
    }

    *BootOrderSize -= sizeof (UINT16);
  }

  return Status;

}

/*
 * Removes firmware legacy options deemed invalid by this implementation;
 * this is a persistent firmware mutation and is not equivalent to UI hiding.
 */
EFI_STATUS
BdsDeleteAllInvalidLegacyBootOptions (
  VOID
  )
{
  UINT16                    *BootOrder;
  UINT8                     *BootOptionVar;
  UINTN                     BootOrderSize;
  UINTN                     BootOptionSize;
  EFI_STATUS                Status;
  UINT16                    HddCount;
  UINT16                    BbsCount;
  HDD_INFO                  *LocalHddInfo;
  BBS_TABLE                 *LocalBbsTable;
  BBS_TABLE                 *BbsEntry;
  UINT16                    BbsIndex;
  EFI_LEGACY_BIOS_PROTOCOL  *LegacyBios;
  UINTN                     Index;
  UINT16                    BootOption[10];
  UINT16                    BootDesc[100];
  BOOLEAN                   DescStringMatch;

  Status        = EFI_SUCCESS;
  BootOrder     = NULL;
  BootOrderSize = 0;
  HddCount      = 0;
  BbsCount      = 0;
  LocalHddInfo  = NULL;
  LocalBbsTable = NULL;
  BbsEntry      = NULL;

  Status        = EfiLibLocateProtocol (&gEfiLegacyBiosProtocolGuid, (VOID **) &LegacyBios);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  refit_call5_wrapper(LegacyBios->GetBbsInfo,
                LegacyBios,
                &HddCount,
                &LocalHddInfo,
                &BbsCount,
                &LocalBbsTable
                );

  BootOrder = BdsLibGetVariableAndSize (
                L"BootOrder",
                &EfiGlobalVariableGuid,
                &BootOrderSize
                );
  if (BootOrder == NULL) {
    BootOrderSize = 0;
  }

  Index = 0;
  while (Index < BootOrderSize / sizeof (UINT16)) {
    SPrint (BootOption, sizeof (BootOption), L"Boot%04x", BootOrder[Index]);
    BootOptionVar = BdsLibGetVariableAndSize (
                      BootOption,
                      &EfiGlobalVariableGuid,
                      &BootOptionSize
                      );
    if (NULL == BootOptionVar) {
      BootOptionSize = 0;
      Status = refit_call5_wrapper(gRT->GetVariable,
                      BootOption,
                      &EfiGlobalVariableGuid,
                      NULL,
                      &BootOptionSize,
                      BootOptionVar
                      );
      if (Status == EFI_NOT_FOUND) {

        BdsDeleteBootOption (
          BootOrder[Index],
          BootOrder,
          &BootOrderSize
          );
        continue;
      } else {
        MyFreePool (BootOrder);
        return EFI_OUT_OF_RESOURCES;
      }
    }

    if (!BdsIsLegacyBootOption (BootOptionVar, &BbsEntry, &BbsIndex)) {
      if (BootOptionVar!= NULL) {
        MyFreePool (BootOptionVar);
      }
      Index++;
      continue;
    }

    if (BbsIndex < BbsCount) {

      DescStringMatch = FALSE;
      BdsBuildLegacyDevNameString (
        &LocalBbsTable[BbsIndex],
        BbsIndex,
        sizeof (BootDesc),
        BootDesc
        );

      if (StrCmp (BootDesc, (UINT16*)(BootOptionVar + sizeof (UINT32) + sizeof (UINT16))) == 0) {
        DescStringMatch = TRUE;
      }

      if (!((LocalBbsTable[BbsIndex].BootPriority == BBS_IGNORE_ENTRY) ||
            (LocalBbsTable[BbsIndex].BootPriority == BBS_DO_NOT_BOOT_FROM)) &&
          (LocalBbsTable[BbsIndex].DeviceType == BbsEntry->DeviceType) &&
          DescStringMatch) {
        Index++;
        continue;
      }
    }

    if (BootOptionVar != NULL) {
      MyFreePool (BootOptionVar);
    }

    BdsDeleteBootOption (
      BootOrder[Index],
      BootOrder,
      &BootOrderSize
      );
  }

  if (BootOrderSize != 0) {
    Status = EfivarSetRaw(&EfiGlobalVariableGuid, L"BootOrder",
                          (CHAR8*) BootOrder,
                          BootOrderSize, TRUE);
  } else {
    EfiLibDeleteVariable (L"BootOrder", &EfiGlobalVariableGuid);
  }

  if (BootOrder != NULL) {
    MyFreePool (BootOrder);
  }

  return Status;
}

UINT16 *
BdsFillDevOrderBuf (
  IN BBS_TABLE                    *BbsTable,
  IN BBS_TYPE                     BbsType,
  IN UINTN                        BbsCount,
  OUT UINT16                      *Buf
  )
{
  UINTN Index;

  for (Index = 0; Index < BbsCount; Index++) {
    if (BbsTable[Index].BootPriority == BBS_IGNORE_ENTRY) {
      continue;
    }

    if (BbsTable[Index].DeviceType != BbsType) {
      continue;
    }

    *Buf = (UINT16) (Index & 0xFF);
    Buf++;
  }

  return Buf;
}
