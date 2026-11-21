/*
 * EFI/legacy loader scanning and submenu population interface.
 * Entries represent discovered candidates across available firmware volumes,
 * not a hardcoded number of OSs. Hidden entries are UI filtering, not writes
 * to the loader files or firmware's one-time boot alternatives.
 */
 


































 






 














#ifndef __SCAN_H_
#define __SCAN_H_

LOADER_ENTRY *InitializeLoaderEntry(IN LOADER_ENTRY *Entry);
REFIT_MENU_SCREEN *InitializeSubScreen(IN LOADER_ENTRY *Entry);
VOID GenerateSubScreen(LOADER_ENTRY *Entry, IN REFIT_VOLUME *Volume, IN BOOLEAN GenerateReturn);
VOID SetLoaderDefaults(LOADER_ENTRY *Entry, CHAR16 *LoaderPath, IN REFIT_VOLUME *Volume);
VOID ScanForBootloaders(BOOLEAN ShowMessage);
VOID FindTool(CHAR16 *Locations, CHAR16 *Names, CHAR16 *Description, UINTN Icon);
VOID ScanForTools(VOID);

#endif

 
