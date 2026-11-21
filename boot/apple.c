/*
 * Apple firmware interoperability, including CSR/SIP-related state and OS
 * information protocols. These interfaces are vendor-specific and must not
 * be assumed present on non-Apple UEFI platforms.
 */

#include "global.h"
#include "config.h"
#include "lib.h"
#include "screen.h"
#include "apple.h"
#include "log.h"
#include "mystrings.h"
#include "../include/refit_call_wrapper.h"

CHAR16 gCsrStatus[512];

/*
 * Reads the firmware-side CSR state when the relevant Apple interface
 * exists. An unavailable variable is not proof that security is disabled.
 */
EFI_STATUS GetCsrStatus(UINT32 *CsrStatus) {
    UINT32     *ReturnValue = NULL;
    UINTN      CsrLength;
    EFI_GUID   CsrGuid = CSR_GUID;
    EFI_STATUS Status = EFI_INVALID_PARAMETER;

    if (CsrStatus) {
        Status = EfivarGetRaw(&CsrGuid, L"csr-active-config", (CHAR8**) &ReturnValue, &CsrLength);
        if (Status == EFI_SUCCESS) {
            if (CsrLength == 4) {
                *CsrStatus = *ReturnValue;
            } else {
                Status = EFI_BAD_BUFFER_SIZE;
                SPrint(gCsrStatus, 255, L" Unknown System Integrity Protection version");
            }
            MyFreePool(ReturnValue);
        } else if ((Status == EFI_NOT_FOUND) && (StriSubCmp(L"Apple", ST->FirmwareVendor))) {
            *CsrStatus = SIP_ENABLED;
            Status = EFI_SUCCESS;
        }  
    }  
    return Status;
}  

VOID RecordgCsrStatus(UINT32 CsrStatus, BOOLEAN DisplayMessage) {
    EG_PIXEL    BGColor = COLOR_LIGHTBLUE;

    switch (CsrStatus) {
        case SIP_ENABLED:
            SPrint(gCsrStatus, 255, L" System Integrity Protection is enabled (0x%02x)", CsrStatus);
            break;
        case SIP_DISABLED_10:
            SPrint(gCsrStatus, 255, L" System Integrity Protection is disabled for OS X 10.x (0x%02x)",
                   CsrStatus);
            break;
        case SIP_DISABLED_11:
            SPrint(gCsrStatus, 255, L" System Integrity Protection is disabled (0x%02x)", CsrStatus);
            break;
        default:
            SPrint(gCsrStatus, 255, L" System Integrity Protection status: 0x%02x", CsrStatus);
    }  
    if (DisplayMessage) {
        egDisplayMessage(gCsrStatus, &BGColor, CENTER);
        PauseSeconds(3);
    }  
}  

/**
 * Advance from the current four-byte csr-active-config value to the next
 * configured mask. A missing match or the end of the list selects its head.
 * Refresh the displayed status only after the persistent variable write succeeds.
 */
VOID RotateCsrValue(VOID) {
    UINT32       CurrentValue, TargetCsr;
    UINT32_LIST  *ListItem;
    EFI_GUID     CsrGuid = CSR_GUID;
    EFI_STATUS   Status;

    LOG(1, LOG_LINE_SEPARATOR, L"Rotating CSR value");
    Status = GetCsrStatus(&CurrentValue);
    if ((Status == EFI_SUCCESS) && GlobalConfig.CsrValues) {
        ListItem = GlobalConfig.CsrValues;
        while ((ListItem != NULL) && (ListItem->Value != CurrentValue))
            ListItem = ListItem->Next;
        if (ListItem == NULL || ListItem->Next == NULL) {
            TargetCsr = GlobalConfig.CsrValues->Value;
        } else {
            TargetCsr = ListItem->Next->Value;
        }
        LOG(1, LOG_LINE_NORMAL, L"CSR value was 0x%04x; setting to 0x%04x", CurrentValue, TargetCsr);
        Status = EfivarSetRaw(&CsrGuid, L"csr-active-config", (CHAR8 *) &TargetCsr, 4, TRUE);
        if (Status == EFI_SUCCESS) {
            RecordgCsrStatus(TargetCsr, TRUE);
            LOG(2, LOG_LINE_NORMAL, L"Successful setting of CSR value of 0x%04x", TargetCsr);
        } else {
            SPrint(gCsrStatus, 255, L" Error setting System Integrity Protection code.");
            LOG(1, LOG_LINE_NORMAL, gCsrStatus);
        }
    } else {
        LOG(1, LOG_LINE_NORMAL, L"Could not retrieve CSR value or csr_values not set");
    }  
}  

#define EFI_APPLE_SET_OS_PROTOCOL_GUID  \
  { 0xc5c5da95, 0x7d5c, 0x45e6, \
    { 0xb2, 0xf1, 0x3f, 0xd5, 0x2b, 0xb1, 0x00, 0x77 } \
  }

typedef struct EfiAppleSetOsInterface {
    UINT64 Version;
    EFI_STATUS EFIAPI (*SetOsVersion) (IN CHAR8 *Version);
    EFI_STATUS EFIAPI (*SetOsVendor) (IN CHAR8 *Vendor);
} EfiAppleSetOsInterface;

/**
 * An absent Apple protocol is a successful no-op so a single launch path works
 * across firmware vendors. A configured version enables the ASCII OS name call.
 * Protocol revision 2 or later also accepts the vendor name after the version
 * call succeeds.
 */
EFI_STATUS SetAppleOSInfo() {
    CHAR16 *AppleOSVersion = NULL;
    CHAR8 *AppleOSVersion8 = NULL;
    EFI_STATUS Status;
    EFI_GUID apple_set_os_guid = EFI_APPLE_SET_OS_PROTOCOL_GUID;
    EfiAppleSetOsInterface *SetOs = NULL;

    LOG(1, LOG_LINE_NORMAL, L"Setting Apple OS information, if applicable");
    Status = refit_call3_wrapper(BS->LocateProtocol, &apple_set_os_guid, NULL, (VOID**) &SetOs);

    if ((Status != EFI_SUCCESS) || (!SetOs)) {
        LOG(2, LOG_LINE_NORMAL, L"Not a Mac; not setting Apple OS information");
        return EFI_SUCCESS;
    }

    if ((SetOs->Version != 0) && GlobalConfig.SpoofOSXVersion) {
        AppleOSVersion = StrDuplicate(L"Mac OS X");
        MergeStrings(&AppleOSVersion, GlobalConfig.SpoofOSXVersion, ' ');
        if (AppleOSVersion) {
            LOG(2, LOG_LINE_NORMAL, L"Setting Apple OS information to '%s'", AppleOSVersion);
            AppleOSVersion8 = AllocateZeroPool((StrLen(AppleOSVersion) + 1) * sizeof(CHAR8));
            UnicodeStrToAsciiStr(AppleOSVersion, AppleOSVersion8);
            if (AppleOSVersion8) {
                Status = refit_call1_wrapper (SetOs->SetOsVersion, AppleOSVersion8);
                if (!EFI_ERROR(Status))
                    Status = EFI_SUCCESS;
                MyFreePool(AppleOSVersion8);
            } else {
                Status = EFI_OUT_OF_RESOURCES;
                Print(L"Out of resources in SetAppleOSInfo!\n");
            }
            if ((Status == EFI_SUCCESS) && (SetOs->Version >= 2))
                Status = refit_call1_wrapper (SetOs->SetOsVendor, (CHAR8 *) "Apple Inc.");
            MyFreePool(AppleOSVersion);
        }  
    }  
    if (Status != EFI_SUCCESS)
        Print(L"Unable to set firmware boot type!\n");

    return (Status);
}  
