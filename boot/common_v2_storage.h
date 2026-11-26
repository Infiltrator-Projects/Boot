/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_STORAGE_H
#define BOOT_COMMON_V2_STORAGE_H
#include <stdint.h>
#include <stddef.h>

/* BOOT-private device boundary, not a filesystem ABI. Geometry and media
 * identity come from the storage provider, never filesystem metadata. */
typedef struct {
    uint64_t last_block;
    uint32_t media_id, logical_bytes, physical_bytes;
    uint32_t present, read_only;
} BootV2Geometry;
typedef struct {
    void *context;
    int (*geometry)(void *, BootV2Geometry *);
    int (*read)(void *, uint32_t media_id, uint64_t offset, void *, uint32_t);
    int (*write)(void *, uint32_t media_id, uint64_t offset, const void *, uint32_t);
    int (*flush)(void *, uint32_t media_id);
    /* Must revalidate a real exclusive, unmounted device lease and durable
     * backend at each call. A Boolean stored in a descriptor is insufficient. */
    int (*check_write_lease)(void *, uint32_t media_id);
    void *(*allocate)(void *, size_t);
    void (*release)(void *, void *);
} BootV2Backend;
typedef struct {
    BootV2Backend backend;
    BootV2Geometry geometry;
    uint64_t volume_bytes;
    uint32_t maximum_transfer;
    size_t allocations;
    unsigned active, writable, stale;
} BootV2Storage;
/* Caller retains the backend context and prevents concurrent access until
 * the mounted volume has unmounted and all module allocations are released. */
int boot_v2_storage_init(BootV2Storage *, const BootV2Backend *, uint32_t maximum_transfer, int want_write);
void boot_v2_storage_close(BootV2Storage *);
int boot_v2_storage_read(void *, uint64_t, void *, uint32_t);
int boot_v2_storage_write(void *, uint64_t, const void *, uint32_t);
int boot_v2_storage_flush(void *);
void *boot_v2_storage_allocate(void *, size_t);
void boot_v2_storage_release(void *, void *);
#endif
