/*
 * Discovery builds menu entries from a volume and loader pathname. Generated
 * submenus hold alternate options and folded kernels for that entry. Actual
 * firmware image loading is deferred until the menu dispatches a selection.
 */

#ifndef __SCAN_H_
#define __SCAN_H_

/*
 * Allocate a loader record, optionally taking initial state from a template.
 * Copied image, submenu and volume pointers do not become independently owned
 * objects; the caller must retain the surrounding discovery inventory.
 */
LOADER_ENTRY *InitializeLoaderEntry(IN LOADER_ENTRY *Entry);
/*
 * Attach subordinate menu choices to the parent loader. Entries may share
 * volume and image pointers with that parent, and become stale on rescan.
 */
REFIT_MENU_SCREEN *InitializeSubScreen(IN LOADER_ENTRY *Entry);
VOID GenerateSubScreen(LOADER_ENTRY *Entry, IN REFIT_VOLUME *Volume, IN BOOLEAN GenerateReturn);
/*
 * Derive icons, Linux options and OS labels heuristically from a borrowed
 * path and volume. This does not authenticate an EFI loader. The subsequent
 * scan recreates the menu, invalidating all prior selection pointers.
 */
VOID SetLoaderDefaults(LOADER_ENTRY *Entry, CHAR16 *LoaderPath, IN REFIT_VOLUME *Volume);
VOID ScanForBootloaders(BOOLEAN ShowMessage);
/*
 * Tool lookup uses comma-separated candidate paths and names. Its current
 * directory-text deduplication is not volume-qualified: a matching directory
 * on a later volume may be skipped. A discovered tool is not yet executed.
 */
VOID FindTool(CHAR16 *Locations, CHAR16 *Names, CHAR16 *Description, UINTN Icon);
VOID ScanForTools(VOID);

#endif
