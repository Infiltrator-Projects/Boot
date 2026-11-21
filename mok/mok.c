 










 


































#include "global.h"
#include "mok.h"
#include "../include/refit_call_wrapper.h"
#include "../boot/lib.h"
#include "../boot/screen.h"


 


BOOLEAN secure_mode (VOID)
{
   EFI_STATUS status;
   EFI_GUID global_var = EFI_GLOBAL_VARIABLE;
   UINTN charsize = sizeof(char);
   UINT8 *sb = NULL, *setupmode = NULL;

   status = EfivarGetRaw(&global_var, L"SecureBoot", (CHAR8 **) &sb, &charsize);
    
   if (status != EFI_SUCCESS || charsize != sizeof(CHAR8) || *sb != 1) {
      return FALSE;
   }

   status = EfivarGetRaw(&global_var, L"SetupMode", (CHAR8 **) &setupmode, &charsize);
   if (status == EFI_SUCCESS && charsize == sizeof(CHAR8) && *setupmode == 1) {
      return FALSE;
   }

   return TRUE;
}  

 
 
BOOLEAN ShimLoaded(void) {
   SHIM_LOCK   *shim_lock;
   EFI_GUID    ShimLockGuid = SHIM_LOCK_GUID;

   return (refit_call3_wrapper(BS->LocateProtocol, &ShimLockGuid, NULL, (VOID**) &shim_lock) == EFI_SUCCESS);
}  

 
 
 
BOOLEAN ShimValidate (VOID *data, UINT32 size)
{
   SHIM_LOCK   *shim_lock;
   EFI_GUID    ShimLockGuid = SHIM_LOCK_GUID;

   if ((data != NULL) && (refit_call3_wrapper(BS->LocateProtocol, &ShimLockGuid, NULL, (VOID**) &shim_lock) == EFI_SUCCESS)) {
      if (!shim_lock)
         return FALSE;

      if (shim_lock->shim_verify(data, size) == EFI_SUCCESS)
         return TRUE;
   }

   return FALSE;
}  