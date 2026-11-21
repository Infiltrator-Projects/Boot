/*
 * Secure Boot and shim-mediated validation helpers. Firmware Secure Boot
 * state, shim presence and trust-path validation are distinct conditions.
 * A successful menu selection is insufficient to establish executable trust.
 */

#include "global.h"
#include "mok.h"
#include "../include/refit_call_wrapper.h"
#include "../boot/lib.h"
#include "../boot/screen.h"

/*
 * Observes security-related firmware state; an unavailable variable must
 * not be interpreted as approval to bypass image authentication.
 */
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

/*
 * Test the status of locating shim's lock protocol. This lookup supports the
 * choice of handoff path. Individual images are checked separately, using
 * their actual bytes in ShimValidate.
 */
BOOLEAN ShimLoaded(void) {
   SHIM_LOCK   *shim_lock;
   EFI_GUID    ShimLockGuid = SHIM_LOCK_GUID;

   return (refit_call3_wrapper(BS->LocateProtocol, &ShimLockGuid, NULL, (VOID**) &shim_lock) == EFI_SUCCESS);
}  

/*
 * Look up the shim interface at the time of use. Return TRUE only for a
 * non-null buffer and interface when shim_verify accepts exactly size bytes.
 * A failed protocol lookup and a rejected image both yield FALSE to the
 * caller.
 */
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