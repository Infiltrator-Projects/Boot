/*
 * boot/install.h
 * Headers related to installation of BOOT
 *
 * Copyright (c) 2020-2024 by Roderick W. Smith
 *
 * Distributed under the terms of the GNU General Public License (GPL)
 * version 3 (GPLv3), a copy of which must be distributed with this source
 * code or binaries made from it.
 *
 */

#ifndef __INSTALL_H_
#define __INSTALL_H_

#if defined (EFIX64)
#define INST_DIRECTORIES L"\\EFI,\\EFI\\boot,\\EFI\\boot\\icons,\\EFI\\boot\\drivers_x64"
#define INST_DRIVERS_SUBDIR L"drivers_x64"
#define INST_BOOT_NAME L"boot_x64.efi"
#define INST_PLATFORM_EXTENSION L"_x64.efi"
#elif defined(EFI32)
#define INST_DIRECTORIES L"\\EFI,\\EFI\\boot,\\EFI\\boot\\icons,\\EFI\\boot\\drivers_ia32"
#define INST_DRIVERS_SUBDIR L"drivers_ia32"
#define INST_BOOT_NAME L"boot_ia32.efi"
#define INST_PLATFORM_EXTENSION L"_ia32.efi"
#elif defined(EFIAARCH64)
#define INST_DIRECTORIES L"\\EFI,\\EFI\\boot,\\EFI\\boot\\icons,\\EFI\\boot\\drivers_aa64"
#define INST_DRIVERS_SUBDIR L"drivers_aa64"
#define INST_BOOT_NAME L"boot_aa64.efi"
#define INST_PLATFORM_EXTENSION L"_aa64.efi"
#else
#define INST_DIRECTORIES L"\\EFI,\\EFI\\boot,\\EFI\\boot\\icons,\\EFI\\boot\\drivers"
#define INST_DRIVERS_SUBDIR L"drivers"
#define INST_BOOT_NAME L"boot.efi"
#define INST_PLATFORM_EXTENSION L".efi"
#endif

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
//     CHAR16           *Arguments; // Part of original data structure, but we don't use
} EFI_BOOT_ENTRY;

// A linked-list data structure intended to hold a list of all the EFI boot
// entries on the computer....
typedef struct _boot_entry_list {
    EFI_BOOT_ENTRY           BootEntry;
    struct _boot_entry_list  *NextBootEntry;
} BOOT_ENTRY_LIST;

EFI_STATUS BackupOldFile(IN EFI_FILE_PROTOCOL *BaseDir, CHAR16 *FileName);
VOID InstallBoot(VOID);
BOOT_ENTRY_LIST * FindBootOrderEntries(VOID);
VOID DeleteBootOrderEntries(BOOT_ENTRY_LIST *Entries);
VOID ManageBootorder(VOID);

#endif
