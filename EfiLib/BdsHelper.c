/*
 * Legacy BDS interoperability helpers connecting firmware option data with
 * BIOS boot-services tables. Hardware/firmware capability is a prerequisite;
 * legacy mappings cannot be inferred from native UEFI image discovery.
 */
 




 












#include "BdsHelper.h"
#include "legacy.h"
#include "mystrings.h"
#include "../boot/screen.h"
#include "../boot/lib.h"
#include "../include/refit_call_wrapper.h"

EFI_GUID gEfiLegacyBootProtocolGuid     = { 0xdb9a1e3d, 0x45cb, 0x4abb, { 0x85, 0x3b, 0xe5, 0x38, 0x7f, 0xdb, 0x2e, 0x2d }};

 















/*
 * Adjusts BBS table information for a requested legacy boot option.
 * Firmware and device-order semantics must agree with the selected entry.
 */
VOID UpdateBbsTable (BDS_COMMON_OPTION *Option) {
   UINT16  Idx;
   EFI_LEGACY_BIOS_PROTOCOL  *LegacyBios;
   EFI_STATUS                Status;
   UINT16                       HddCount = 0;
   HDD_INFO                     *HddInfo = NULL; 
   UINT16                       BbsCount = 0; 
   BBS_TABLE                 *LocalBbsTable = NULL;
   BBS_BBS_DEVICE_PATH       *OptionBBS;
   CHAR16                    Desc[100];

   Status = refit_call3_wrapper(gBS->LocateProtocol, &gEfiLegacyBootProtocolGuid, NULL, (VOID **) &LegacyBios);
   if (EFI_ERROR (Status) || (Option == NULL)) {
      return;
   }

   OptionBBS = (BBS_BBS_DEVICE_PATH *) Option->DevicePath;
   Status = refit_call5_wrapper(LegacyBios->GetBbsInfo, LegacyBios, &HddCount, &HddInfo, &BbsCount, &LocalBbsTable);

 
 
 

   for (Idx = 0; Idx < BbsCount; Idx++) {
      if(LocalBbsTable[Idx].DeviceType == 0) {
         continue;
      }

      BdsBuildLegacyDevNameString (&LocalBbsTable[Idx], Idx, sizeof (Desc), Desc);

       
      if (LocalBbsTable[Idx].DeviceType == OptionBBS->DeviceType) {
         if (MyStriCmp(Desc, Option->Description)) {
             
            LocalBbsTable[Idx].BootPriority = 0;
         } else {
             
             
            LocalBbsTable[Idx].BootPriority = 1;
         }  
      } else if (LocalBbsTable[Idx].BootPriority <= 1) {
          
          
         LocalBbsTable[Idx].BootPriority = 2;
      }  

 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 

   }  
 
}

 









EFI_STATUS
BdsLibDoLegacyBoot (
  IN  BDS_COMMON_OPTION           *Option
  )
{
    EFI_STATUS                Status;
    EFI_LEGACY_BIOS_PROTOCOL  *LegacyBios;

    Status = refit_call3_wrapper(gBS->LocateProtocol, &gEfiLegacyBootProtocolGuid, NULL, (VOID **) &LegacyBios);
    if (EFI_ERROR (Status)) {
      return EFI_UNSUPPORTED;
    }

    UpdateBbsTable(Option);

    return refit_call4_wrapper(LegacyBios->LegacyBoot, LegacyBios, (BBS_BBS_DEVICE_PATH *) Option->DevicePath,
                                  Option->LoadOptionsSize, Option->LoadOptions);
}
