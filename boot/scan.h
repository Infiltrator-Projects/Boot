/*
 * BOOT / Loader discovery API
 *
 * Loader discovery and submenu construction interface.
 *
 * Entry records are tied to the current scanned volume inventory.
 * Rebuilding menus invalidates previous entry pointers.
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
/*
 * Materialise alternate boot options in Entry's submenu. The generated
 * choices share the parent volume/inventory lifetime; GenerateReturn adds a
 * navigation entry. Rebuilding discovery invalidates menu-owned pointers.
 */
VOID GenerateSubScreen(LOADER_ENTRY *Entry, IN REFIT_VOLUME *Volume, IN BOOLEAN GenerateReturn);
/*
 * Derive icons, Linux options and OS labels heuristically from a borrowed
 * path and volume. This does not authenticate an EFI loader. The subsequent
 * scan recreates the menu, invalidating all prior selection pointers.
 */
VOID SetLoaderDefaults(LOADER_ENTRY *Entry, CHAR16 *LoaderPath, IN REFIT_VOLUME *Volume);
/*
 * Append candidates to MainMenu in GlobalConfig.ScanFor order. Hidden-tag
 * exclusions are temporarily merged and then restored; discovery is not
 * necessarily side-effect free (the network path can invoke an EFI image).
 * Shortcut numbers depend on the resulting menu order, not stable OS IDs.
 */
VOID ScanForBootloaders(BOOLEAN ShowMessage);
/*
 * Tool lookup uses comma-separated candidate paths and names. Its current
 * directory-text deduplication is not volume-qualified: a matching directory
 * on a later volume may be skipped. A discovered tool is not yet executed.
 */
VOID FindTool(CHAR16 *Locations, CHAR16 *Names, CHAR16 *Description, UINTN Icon);
/*
 * Append configured built-in actions and EFI utilities to MainMenu. This
 * also extends ExtraToolLocations with BOOT's own directory; finding a tool
 * neither authenticates nor executes it.
 */
VOID ScanForTools(VOID);

#endif
