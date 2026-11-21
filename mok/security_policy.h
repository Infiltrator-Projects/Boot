/*
 * Install/uninstall interface for BOOT's EFI security policy bridge.
 * These operations affect firmware image-authentication callbacks; call
 * order and error propagation are security rather than cosmetic concerns.
 */
EFI_STATUS
security_policy_install(void);
EFI_STATUS
security_policy_uninstall(void);
 
 
