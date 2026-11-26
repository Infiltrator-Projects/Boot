/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_UEFI_MEMORY_H
#define BOOT_COMMON_V2_UEFI_MEMORY_H
#include <efi.h>
#include "common_v2_memory.h"

typedef struct {
    VOID *protocol;
    EFI_STATUS last_status;
    BootV2Memory memory;
    BootV2ElfHost host;
} BootV2UefiMemory;
/* Requires the UEFI Memory Attribute Protocol and an independent, real
 * authentication callback. Unsupported firmware leaves EFI loading intact. */
EFI_STATUS BootV2UefiMemoryInit(BootV2UefiMemory *,void *,
                              int (*)(void *,const void *,size_t));
#endif
