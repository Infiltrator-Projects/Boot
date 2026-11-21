/*
 * Installation saves firmware's current authentication callbacks before
 * replacing them. Both operations return EFI_STATUS. Restoration can fail
 * after one of the two interfaces has already been restored, so status
 * matters at teardown as well as setup.
 */
EFI_STATUS
security_policy_install(void);
EFI_STATUS
security_policy_uninstall(void);
