/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_MODULE_H
#define BOOT_COMMON_V2_MODULE_H
#include "common_v2_elf.h"
#include <infiltratr/filesystem_module_v2.h>

/* A validated view borrows the image. The owner must keep that image mapped
 * until all volumes and file handles have been closed and unmounted. */
typedef struct {
    const BootV2ElfImage *image;
    const IfsFsModuleApiV2 *api;
    const IfsFsGroupHeaderV2 *groups[IFS_FS_GROUP_MAX+1];
} BootV2ModuleView;

/* Call only on an authenticated image returned by boot_v2_elf_load.
 * Checks all returned pointers before dereferencing them, snapshots each
 * group once, and never treats declared capabilities as media qualification. */
int boot_v2_module_inspect(const BootV2ElfImage *, const char *expected_id,
                           BootV2ModuleView *);
const IfsFsCoreOpsV2 *boot_v2_module_readable_core(const BootV2ModuleView *);
#endif
