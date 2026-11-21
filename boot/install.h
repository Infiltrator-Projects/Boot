 











#ifndef __INSTALL_H_
#define __INSTALL_H_

#define EFI_BOOT_OPTION_DO_NOTHING   0
#define EFI_BOOT_OPTION_MAKE_DEFAULT 1
#define EFI_BOOT_OPTION_DELETE       2

#ifdef __MAKEWITH_TIANO
#define DevicePathSize GetDevicePathSize
#endif

typedef struct {
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

EFI_STATUS BackupOldFile(IN EFI_FILE_PROTOCOL *BaseDir, CHAR16 *FileName);
BOOT_ENTRY_LIST * FindBootOrderEntries(VOID);
VOID DeleteBootOrderEntries(BOOT_ENTRY_LIST *Entries);
VOID ManageBootorder(VOID);

#endif
