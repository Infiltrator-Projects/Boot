/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_SO_ELF_H
#define BOOT_SO_ELF_H
#include <stddef.h>
#include <stdint.h>
#include "../include/ifs_boot_module.h"

#define BOOT_SO_MAX_FILE (16u*1024u*1024u)
#define BOOT_SO_MAX_IMAGE (32u*1024u*1024u)
#define BOOT_SO_MAX_SEGMENTS 16u
#define BOOT_SO_READ 4u
#define BOOT_SO_WRITE 2u
#define BOOT_SO_EXECUTE 1u
typedef struct {
    uint64_t address, bytes;
    uint32_t flags;
} BootSoSegment;
typedef struct {
    void *context;
    void *(*allocate)(void *, size_t);
    void (*release)(void *, void *, size_t);
    /* Optional platform page protection; declaration-level W^X is always
     * checked. A platform callback must apply protections or report failure. */
    int (*protect)(void *, void *, size_t, uint32_t);
} BootSoMemory;
typedef struct {
    void *base;
    size_t bytes;
    BootSoMemory memory;
    IfsBootManifest manifest;
    const IfsBootModuleV1 *api;
    BootSoSegment segments[BOOT_SO_MAX_SEGMENTS];
    uint32_t segment_count;
} BootSoImage;

/* Validate without executing or allocating. Execution is permitted only after
 * the caller has separately authorised the bytes under local trust policy. */
int BootSoValidate(const void *, size_t);
int BootSoLoad(const void *, size_t, const BootSoMemory *, BootSoImage *);
void BootSoUnload(BootSoImage *);
int BootSoContains(const BootSoImage *, const void *, size_t, uint32_t, uint32_t);
#endif
