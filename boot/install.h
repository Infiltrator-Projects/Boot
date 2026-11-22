/*
 * BOOT / Firmware boot option API
 *
 * Firmware boot-option and EFI variable interface.
 *
 * Provides Boot#### inspection, ordering and related firmware operations.
 * Host-side installation onto the ESP is a separate component.
 */

#ifndef __INSTALL_H_
#define __INSTALL_H_

#define EFI_BOOT_OPTION_DO_NOTHING   0
#define EFI_BOOT_OPTION_MAKE_DEFAULT 1
#define EFI_BOOT_OPTION_DELETE       2

#ifdef __MAKEWITH_TIANO
#define DevicePathSize GetDevicePathSize
#endif

typedef struct {
    /*
     * BootNum identifies the Boot#### variable. Size counts the device-path
     * list in bytes. The parser duplicates the UTF-16 label and path
     * separately because their original firmware variable buffer is released.
     */
    UINT16           BootNum;
    UINT32           Options;
    UINT16           Size;
    CHAR16           *Label;
    EFI_DEVICE_PATH  *DevPath;

} EFI_BOOT_ENTRY;

typedef struct _boot_entry_list {
    EFI_BOOT_ENTRY           BootEntry;
    struct _boot_entry_list  *NextBootEntry;
} BOOT_ENTRY_LIST;

/*
 * BackupOldFile renames a writable EFI directory entry to its '-old' sibling.
 * Existing backup names are not overwritten; the opened handle is closed.
 */
EFI_STATUS BackupOldFile(IN EFI_FILE_PROTOCOL *BaseDir, CHAR16 *FileName);
/*
 * Returns a caller-owned list of copied UTF-16 labels and device paths.
 * Invalid firmware records are omitted. NULL means an empty or unreadable list,
 * not an authenticated claim that no Boot#### records exist.
 * DeleteBootOrderEntries releases the list, not the EFI variables.
 */
BOOT_ENTRY_LIST * FindBootOrderEntries(VOID);
VOID DeleteBootOrderEntries(BOOT_ENTRY_LIST *Entries);
/* May change persistent Boot#### and BootOrder; errors appear in the EFI UI. */
VOID ManageBootorder(VOID);

#endif
