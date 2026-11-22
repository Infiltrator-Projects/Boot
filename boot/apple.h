/*
 * Vendor-specific Apple EFI variables and OS information protocol declarations.
 * These protocols and CSR fields are not universally present on UEFI systems;
 * a failed lookup is not a general-purpose Secure Boot state determination.
 */

#ifndef __APPLE_H_
#define __APPLE_H_

#define CSR_GUID { 0x7c436110, 0xab2a, 0x4bbb, { 0xa8, 0x80, 0xfe, 0x41, 0x99, 0x5c, 0x9f, 0x82 } };
/*
 * Each bit relaxes one SIP restriction. csr-active-config stores the combined
 * 32-bit mask. Rotating the configured values writes a complete mask, so
 * combinations can differ in more than one restriction.
 */
#define CSR_ALLOW_UNTRUSTED_KEXTS            0x01
#define CSR_ALLOW_UNRESTRICTED_FS            0x02
#define CSR_ALLOW_TASK_FOR_PID               0x04
#define CSR_ALLOW_KERNEL_DEBUGGER            0x08
#define CSR_ALLOW_APPLE_INTERNAL             0x10
#define CSR_ALLOW_UNRESTRICTED_DTRACE        0x20
#define CSR_ALLOW_UNRESTRICTED_NVRAM         0x40
#define CSR_ALLOW_DEVICE_CONFIGURATION       0x80
#define CSR_ALLOW_ANY_RECOVERY_OS            0x100
#define CSR_ALLOW_UNAPPROVED_KEXTS           0x200
#define CSR_ALLOW_EXECUTABLE_POLICY_OVERRIDE 0x400
#define CSR_ALLOW_UNAUTHENTICATED_ROOT       0x800
#define CSR_END_OF_LIST                      0xFFFFFFFF

#define SIP_ENABLED  CSR_ALLOW_APPLE_INTERNAL

#define SIP_DISABLED_10 (CSR_ALLOW_UNRESTRICTED_NVRAM | \
                         CSR_ALLOW_UNRESTRICTED_DTRACE | \
                         CSR_ALLOW_APPLE_INTERNAL | \
                         CSR_ALLOW_TASK_FOR_PID | \
                         CSR_ALLOW_UNRESTRICTED_FS | \
                         CSR_ALLOW_UNTRUSTED_KEXTS)

#define SIP_DISABLED_11 (CSR_ALLOW_UNRESTRICTED_NVRAM | \
                         CSR_ALLOW_UNRESTRICTED_DTRACE | \
                         CSR_ALLOW_APPLE_INTERNAL | \
                         CSR_ALLOW_TASK_FOR_PID | \
                         CSR_ALLOW_UNRESTRICTED_FS | \
                         CSR_ALLOW_UNTRUSTED_KEXTS | \
                         CSR_ALLOW_UNAUTHENTICATED_ROOT)
#define CSR_MAX_LEGAL_VALUE (CSR_ALLOW_UNTRUSTED_KEXTS | \
                             CSR_ALLOW_UNRESTRICTED_FS | \
                             CSR_ALLOW_TASK_FOR_PID | \
                             CSR_ALLOW_KERNEL_DEBUGGER | \
                             CSR_ALLOW_APPLE_INTERNAL | \
                             CSR_ALLOW_UNRESTRICTED_DTRACE | \
                             CSR_ALLOW_UNRESTRICTED_NVRAM | \
                             CSR_ALLOW_DEVICE_CONFIGURATION | \
                             CSR_ALLOW_ANY_RECOVERY_OS | \
                             CSR_ALLOW_UNAPPROVED_KEXTS | \
                             CSR_ALLOW_EXECUTABLE_POLICY_OVERRIDE | \
                             CSR_ALLOW_UNAUTHENTICATED_ROOT)

extern CHAR16 gCsrStatus[512];

/*
 * GetCsrStatus queries Apple's csr-active-config; RecordgCsrStatus formats
 * an observed value, while RotateCsrValue may write a new NVRAM mask. A
 * displayed CSR state cannot guarantee SIP enforcement.
 */
EFI_STATUS GetCsrStatus(UINT32 *CsrValue);
VOID RecordgCsrStatus(UINT32 CsrStatus, BOOLEAN DisplayMessage);
VOID RotateCsrValue(VOID);
/*
 * SetAppleOSInfo publishes compatibility metadata through Apple-specific
 * EFI protocols. Its EFI_STATUS represents that operation, not whether
 * macOS can boot from a selected disk.
 */
EFI_STATUS SetAppleOSInfo();

#endif
