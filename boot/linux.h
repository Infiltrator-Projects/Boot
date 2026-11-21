/*
 * Linux EFI-stub metadata, initrd matching and kernel submenu contracts.
 * Autodiscovered initrd names and distributions are heuristics; an appropriate
 * root= and initrd= combination depends on each selected installation.
 * Returned option strings require explicit ownership handling.
 */
 


































 






 














#ifndef __BOOT_LINUX_H_
#define __BOOT_LINUX_H_

CHAR16 * FindInitrd(IN CHAR16 *LoaderPath, IN REFIT_VOLUME *Volume);
CHAR16 *AddInitrdToOptions(CHAR16 *Options, CHAR16 *InitrdPath);
CHAR16 * GetMainLinuxOptions(IN CHAR16 * LoaderPath, IN REFIT_VOLUME *Volume);
VOID GuessLinuxDistribution(CHAR16 **OSIconName, REFIT_VOLUME *Volume, CHAR16 *LoaderPath);
VOID AddKernelToSubmenu(LOADER_ENTRY * TargetLoader, CHAR16 *FileName, REFIT_VOLUME *Volume);
BOOLEAN HasSignedCounterpart(IN REFIT_VOLUME *Volume, IN CHAR16 *FullName);

#endif

 
