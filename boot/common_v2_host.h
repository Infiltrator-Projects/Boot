/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_HOST_H
#define BOOT_COMMON_V2_HOST_H
#include "common_v2_storage.h"
#include <infiltratr/filesystem_module_v2.h>

/* The canonical structure is supplied by Libraries, not duplicated here.
 * The storage object must outlive every callback, including unmount. */
static inline int boot_v2_host_init(IfsFsHostRuntimeV2 *host, BootV2Storage *s)
{
    if (!host || !s || !s->active) return IFS_FS_INVALID;
    *host=(IfsFsHostRuntimeV2){0};
    host->abi_version=IFS_FS_HOST_ABI_V2;
    host->structure_bytes=sizeof(*host);
    host->capabilities=s->writable ? IFS_FS_HOST_EXCLUSIVE|IFS_FS_HOST_DURABLE : IFS_FS_HOST_READ_ONLY;
    host->volume_bytes=s->volume_bytes;
    host->logical_sector_bytes=s->geometry.logical_bytes;
    host->physical_sector_bytes=s->geometry.physical_bytes;
    host->maximum_transfer_bytes=s->maximum_transfer;
    host->context=s;
    host->read_at=boot_v2_storage_read;
    host->allocate=boot_v2_storage_allocate;
    host->release=boot_v2_storage_release;
    if (s->writable) {
        host->write_at=boot_v2_storage_write;
        host->flush=boot_v2_storage_flush;
    }
    return ifs_fs_host_v2_valid(host) ? IFS_FS_OK : IFS_FS_INVALID;
}
#endif
