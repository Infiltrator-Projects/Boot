/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_DISCOVERY_H
#define BOOT_COMMON_V2_DISCOVERY_H
#include <efi.h>
#include "common_v2_module.h"
#include "common_v2_uefi_memory.h"
typedef struct BootV2Provider {
    struct BootV2Provider *next;
    BootV2UefiMemory memory;
    BootV2ElfImage image;
    BootV2ModuleView module;
    char id[IFS_FS_MAX_FORMAT_ID+1];
    UINTN mounted;
} BootV2Provider;
/* Single boot-session registry, sorted by canonical format ID. Successful
 * providers remain mapped while a volume or EFI file handle may borrow them. */
const BootV2Provider *BootV2DiscoverProviders(VOID);
BootV2Provider *BootV2Providers(VOID);
VOID BootV2ReleaseProviders(VOID);
#endif
