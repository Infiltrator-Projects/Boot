/*
 * FindInitrd searches the kernel's directory on the supplied volume.
 * AddInitrdToOptions and GetMainLinuxOptions return allocated command-line
 * strings. HasSignedCounterpart is a filename lookup used during discovery.
 */

#ifndef __BOOT_LINUX_H_
#define __BOOT_LINUX_H_

/*
 * Returned initrd path and composed options are separately allocated for the
 * caller to free. Selection uses filename/version heuristics, not verified
 * initramfs contents. An existing initrd= option takes precedence; %v can
 * select the discovered initrd version. Neither path proves OS boot success.
 */
CHAR16 * FindInitrd(IN CHAR16 *LoaderPath, IN REFIT_VOLUME *Volume);
CHAR16 *AddInitrdToOptions(CHAR16 *Options, CHAR16 *InitrdPath);
CHAR16 * GetMainLinuxOptions(IN CHAR16 * LoaderPath, IN REFIT_VOLUME *Volume);
/*
 * Linux OS icon guesses use naming and os-release metadata, not on-disk
 * signatures. AddKernelToSubmenu creates children tied to its parent
 * menu/volume lifetime and does not transfer the volume's ownership.
 */
VOID GuessLinuxDistribution(CHAR16 **OSIconName, REFIT_VOLUME *Volume, CHAR16 *LoaderPath);
VOID AddKernelToSubmenu(LOADER_ENTRY * TargetLoader, CHAR16 *FileName, REFIT_VOLUME *Volume);
/*
 * A signed counterpart is detected by a nearby filename lookup. TRUE is a
 * discovery hint rather than a certificate-chain or executable-signature
 * verification.
 */
BOOLEAN HasSignedCounterpart(IN REFIT_VOLUME *Volume, IN CHAR16 *FullName);

#endif
