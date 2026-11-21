/*
 * EFI security and Security2 authentication policy interception.
 * Installing or removing these hooks modifies the security protocol path
 * used by child EFI execution, so status and restoration matter. This is
 * not an ordinary display setting or proof of global Secure Boot support.
 */

#include <global.h>

#include "guid.h"
#include "../boot/lib.h"
#include "simple_file.h"
#include "../include/refit_call_wrapper.h"
#include "mok.h"

#include <security_policy.h>

struct _EFI_SECURITY2_PROTOCOL;
struct _EFI_SECURITY_PROTOCOL;
struct _EFI_DEVICE_PATH_PROTOCOL;
typedef struct _EFI_SECURITY2_PROTOCOL EFI_SECURITY2_PROTOCOL;
typedef struct _EFI_SECURITY_PROTOCOL EFI_SECURITY_PROTOCOL;

#if defined(EFIX64)
#define MSABI __attribute__((ms_abi))
#else
#define MSABI
#endif

typedef EFI_STATUS (MSABI *EFI_SECURITY_FILE_AUTHENTICATION_STATE) (
         const EFI_SECURITY_PROTOCOL *This,
         UINT32 AuthenticationStatus,
         const EFI_DEVICE_PATH_PROTOCOL *File
                             );
typedef EFI_STATUS (MSABI *EFI_SECURITY2_FILE_AUTHENTICATION) (
         const EFI_SECURITY2_PROTOCOL *This,
         const EFI_DEVICE_PATH_PROTOCOL *DevicePath,
         VOID *FileBuffer,
         UINTN FileSize,
         BOOLEAN  BootPolicy
                             );

struct _EFI_SECURITY2_PROTOCOL {
   EFI_SECURITY2_FILE_AUTHENTICATION FileAuthentication;
};

struct _EFI_SECURITY_PROTOCOL {
   EFI_SECURITY_FILE_AUTHENTICATION_STATE  FileAuthenticationState;
};

static EFI_SECURITY_FILE_AUTHENTICATION_STATE esfas = NULL;
static EFI_SECURITY2_FILE_AUTHENTICATION es2fa = NULL;

static MSABI EFI_STATUS
/*
 * Routes a Security2 file authentication request through the installed
 * policy. Successful status must reflect accepted validation, not UI intent.
 */
security2_policy_authentication (
   const EFI_SECURITY2_PROTOCOL *This,
   const EFI_DEVICE_PATH_PROTOCOL *DevicePath,
   VOID *FileBuffer,
   UINTN FileSize,
   BOOLEAN  BootPolicy
             )
{
   EFI_STATUS Status;

   Status = uefi_call_wrapper(es2fa, 5, This, DevicePath, FileBuffer, FileSize, BootPolicy);

   if (Status == EFI_SUCCESS)
      return Status;

   if (ShimValidate(FileBuffer, FileSize)) {
      return EFI_SUCCESS;
   } else {
      return Status;
   }
}  

static MSABI EFI_STATUS
security_policy_authentication (
   const EFI_SECURITY_PROTOCOL *This,
   UINT32 AuthenticationStatus,
   const EFI_DEVICE_PATH_PROTOCOL *DevicePathConst
   )
{
   EFI_STATUS        Status;
   EFI_DEVICE_PATH   *DevPath, *OrigDevPath;
   EFI_HANDLE        h;
   EFI_FILE_PROTOCOL *f;
   VOID              *FileBuffer;
   UINTN             FileSize;
   CHAR16            *DevPathStr;

   if (DevicePathConst == NULL) {
      return EFI_INVALID_PARAMETER;
   } else {
      DevPath = OrigDevPath = DuplicateDevicePath((EFI_DEVICE_PATH *)DevicePathConst);
   }

   Status = refit_call3_wrapper(BS->LocateDevicePath, &SIMPLE_FS_PROTOCOL, &DevPath, &h);
   if (Status != EFI_SUCCESS)
      goto out;

   DevPathStr = DevicePathToStr(DevPath);

   Status = simple_file_open_by_handle(h, DevPathStr, &f, EFI_FILE_MODE_READ);
   MyFreePool(DevPathStr);
   if (Status != EFI_SUCCESS)
      goto out;

   Status = simple_file_read_all(f, &FileSize, &FileBuffer);
   simple_file_close(f);
   if (Status != EFI_SUCCESS)
      goto out;

   if (ShimValidate(FileBuffer, FileSize)) {
      Status = EFI_SUCCESS;
   } else {

      Status = uefi_call_wrapper(esfas, 3, This, AuthenticationStatus, DevicePathConst);
   }
   MyFreePool(FileBuffer);

 out:
   MyFreePool(OrigDevPath);
   return Status;
}  

/*
 * The original Security callback saved in esfas doubles as the installation
 * marker. Locate the required Security interface before publishing either
 * replacement. When Security2 is available, save and replace its separate
 * buffer-based callback as well.
 */
EFI_STATUS
security_policy_install(void)
{
   EFI_SECURITY_PROTOCOL *security_protocol;
   EFI_SECURITY2_PROTOCOL *security2_protocol = NULL;
   EFI_STATUS status;

   if (esfas) {

      return EFI_ALREADY_STARTED;
   }

   uefi_call_wrapper(BS->LocateProtocol, 3, &SECURITY2_PROTOCOL_GUID, NULL, (VOID**) &security2_protocol);

   status = uefi_call_wrapper(BS->LocateProtocol, 3, &SECURITY_PROTOCOL_GUID, NULL, (VOID**) &security_protocol);
   if (status != EFI_SUCCESS)

      return status;

   if (security2_protocol) {
      es2fa = security2_protocol->FileAuthentication;
      security2_protocol->FileAuthentication = security2_policy_authentication;
   }

   esfas = security_protocol->FileAuthenticationState;
   security_protocol->FileAuthenticationState = security_policy_authentication;
   return EFI_SUCCESS;
}

/*
 * Restore Security first and clear its saved pointer immediately. If locating
 * Security2 then fails, its saved pointer survives but a later call will see
 * esfas cleared and return EFI_NOT_STARTED. Teardown can therefore be
 * partial.
 */
EFI_STATUS
security_policy_uninstall(void)
{
   EFI_STATUS status;

   if (esfas) {
      EFI_SECURITY_PROTOCOL *security_protocol;

      status = uefi_call_wrapper(BS->LocateProtocol, 3, &SECURITY_PROTOCOL_GUID, NULL, (VOID**) &security_protocol);

      if (status != EFI_SUCCESS)
         return status;

      security_protocol->FileAuthenticationState = esfas;
      esfas = NULL;
   } else {

      return EFI_NOT_STARTED;
   }

   if (es2fa) {
      EFI_SECURITY2_PROTOCOL *security2_protocol;

      status = uefi_call_wrapper(BS->LocateProtocol, 3, &SECURITY2_PROTOCOL_GUID, NULL, (VOID**) &security2_protocol);

      if (status != EFI_SUCCESS)
         return status;

      security2_protocol->FileAuthentication = es2fa;
      es2fa = NULL;
   }

   return EFI_SUCCESS;
}
