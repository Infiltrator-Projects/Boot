/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_UEFI_STORAGE_H
#define BOOT_COMMON_V2_UEFI_STORAGE_H
#include <efi.h>
#include "common_v2_host.h"

typedef struct {
    EFI_HANDLE handle;
    EFI_BLOCK_IO *block;
    EFI_DISK_IO *disk;
    EFI_HANDLE parent_handle;
    EFI_BLOCK_IO *parent;
    UINT32 parent_media_id;
    UINT64 parent_last_block, partition_start, partition_blocks;
    BootV2Storage storage;
    IfsFsHostRuntimeV2 host;
} BootV2UefiStorage;
/* The initial firmware adapter is read-only. The caller must prefer an
 * existing Simple File System and retain this object until module unmount. */
EFI_STATUS BootV2UefiStorageInit(BootV2UefiStorage *, EFI_HANDLE);
VOID BootV2UefiStorageClose(BootV2UefiStorage *);
#endif
