// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BOOT_FSW_OFS_H
#define BOOT_FSW_OFS_H

#define VOLSTRUCTNAME fsw_ofs_volume
#define DNODESTRUCTNAME fsw_ofs_dnode
#include "fsw_core.h"
#include "ofs/vendor/efi/reader.h"

/* The canonical Filesystems reader owns all OFS semantics. */
struct fsw_ofs_volume {
    struct fsw_volume g;
    IfsOfsEfiVolume ofs;
};
struct fsw_ofs_dnode {
    struct fsw_dnode g;
    IfsOfsEfiEntry entry;
    IfsOfsEfiFileCursor sequential;
    int initialized;
};
#endif
