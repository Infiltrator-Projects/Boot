/*
 * BOOT / Legacy BDS API
 *
 * Legacy BDS boot-entry parsing and creation interface.
 *
 * Compatibility code can read or mutate firmware boot variables; OS
 * discovery should not call these mutation paths unless that scan class is
 * explicitly selected.
 */

#include "LegacyBios.h"

#ifndef __LEGACY_H_
#define __LEGACY_H_

#define BBS_MEDIA_PRESENT        0x0800
#define BBS_MEDIA_MAYBE_PRESENT  0x0400

typedef UINT8 BBS_TYPE;

#define VAR_FLAG  EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE

#pragma pack(1)

typedef struct {
   /*
    * BootOptionNumber selects a firmware Boot#### variable. BbsIndex is a
    * slot in the BIOS device table and BbsType identifies its class. Grouping
    * boot options needs this explicit translation between numbering schemes.
    */
   UINT16    BootOptionNumber;
   UINT16    BbsIndex;
   UINT16    BbsType;
} BOOT_OPTION_BBS_MAPPING;
#pragma pack()

#pragma pack(1)
typedef struct {
   BBS_TYPE  BbsType;

   UINT16    Length;
   UINT16    Data[1];
} LEGACY_DEV_ORDER_ENTRY;
#pragma pack()

EFI_STATUS
BdsAddNonExistingLegacyBootOptions (
   VOID
);

EFI_STATUS
BdsDeleteAllInvalidLegacyBootOptions (
  VOID
  );

BOOLEAN
BdsIsLegacyBootOption (
   IN UINT8                 *BootOptionVar,
   OUT BBS_TABLE            **BbsEntry,
   OUT UINT16               *BbsIndex
);

VOID
BdsBuildLegacyDevNameString (
   IN  BBS_TABLE                 *CurBBSEntry,
   IN  UINTN                     Index,
   IN  UINTN                     BufSize,
   OUT CHAR16                    *BootString
);

#endif