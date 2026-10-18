// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Infiltrator Boot OFS adapter: canonical Filesystems reader -> EFI FSW.
 * The OFS on-disk parser lives in Filesystems. This stage is read-only.
 */
#include "fsw_ofs.h"
#include "fsw_efi.h"

extern EFI_GUID gMyEfiBlockIoProtocolGuid;

static fsw_status_t ofs_mount(struct fsw_ofs_volume *v);
static void ofs_free(struct fsw_ofs_volume *v);
static fsw_status_t ofs_volume_stat(struct fsw_ofs_volume *v,
                                    struct fsw_volume_stat *stat);
static fsw_status_t ofs_fill(struct fsw_ofs_volume *v,
                             struct fsw_ofs_dnode *node);
static void ofs_dnode_free(struct fsw_ofs_volume *v,
                            struct fsw_ofs_dnode *node);
static fsw_status_t ofs_dnode_stat(struct fsw_ofs_volume *v,
                                   struct fsw_ofs_dnode *node,
                                   struct fsw_dnode_stat *stat);
static fsw_status_t ofs_extent(struct fsw_ofs_volume *v,
                               struct fsw_ofs_dnode *node,
                               struct fsw_extent *extent);
static fsw_status_t ofs_lookup(struct fsw_ofs_volume *v,
                               struct fsw_ofs_dnode *dir,
                               struct fsw_string *name,
                               struct fsw_ofs_dnode **out);
static fsw_status_t ofs_next(struct fsw_ofs_volume *v,
                             struct fsw_ofs_dnode *dir,
                             struct fsw_shandle *handle,
                             struct fsw_ofs_dnode **out);
static fsw_status_t ofs_readlink(struct fsw_ofs_volume *v,
                                 struct fsw_ofs_dnode *node,
                                 struct fsw_string *target);

struct fsw_fstype_table FSW_FSTYPE_TABLE_NAME(ofs) = {
    { FSW_STRING_TYPE_ISO88591, 3, 3, "OFS" },
    sizeof(struct fsw_ofs_volume),
    sizeof(struct fsw_ofs_dnode),
    ofs_mount, ofs_free, ofs_volume_stat,
    ofs_fill, ofs_dnode_free, ofs_dnode_stat, ofs_extent,
    ofs_lookup, ofs_next, ofs_readlink
};

static int ofs_read_block(void *opaque, uint32_t block, uint8_t bytes[512])
{
    struct fsw_ofs_volume *v = (struct fsw_ofs_volume *)opaque;
    FSW_VOLUME_DATA *host;
    EFI_STATUS result;
    if (!v || block >= v->ofs.blocks)
        return -1;
    host = (FSW_VOLUME_DATA *)v->g.host_data;
    if (!host || !host->DiskIo)
        return -1;
    result = refit_call5_wrapper(host->DiskIo->ReadDisk,
                                 host->DiskIo, host->MediaId,
                                 (UINT64)block * 512ULL,
                                 (UINTN)512, (VOID *)bytes);
    host->LastIOStatus = result;
    return EFI_ERROR(result) ? -1 : 0;
}

static fsw_status_t ofs_mount(struct fsw_ofs_volume *v)
{
    FSW_VOLUME_DATA *host = (FSW_VOLUME_DATA *)v->g.host_data;
    EFI_BLOCK_IO *block_io = NULL;
    EFI_STATUS result;
    struct fsw_string label;
    uint8_t raw[512];
    fsw_status_t status;
    uint32_t count, name_len;
    static char fallback[] = "OFS";

    if (!host)
        return FSW_UNSUPPORTED;
    result = refit_call3_wrapper(BS->HandleProtocol, host->Handle,
                                 &gMyEfiBlockIoProtocolGuid,
                                 (VOID **)&block_io);
    if (EFI_ERROR(result) || !block_io || !block_io->Media ||
        !block_io->Media->MediaPresent ||
        block_io->Media->BlockSize != 512U ||
        block_io->Media->LastBlock < 15ULL ||
        block_io->Media->LastBlock >= 0x7fffffffULL)
        return FSW_UNSUPPORTED;

    count = (uint32_t)(block_io->Media->LastBlock + 1ULL);
    fsw_set_blocksize(v, 512U, 512U);
    if (ifs_ofs_efi_mount(&v->ofs, count, ofs_read_block, v) != 0)
        return FSW_UNSUPPORTED;

    label.type = FSW_STRING_TYPE_ISO88591;
    label.len = label.size = 3;
    label.data = fallback;
    if (ofs_read_block(v, v->ofs.root, raw) == 0) {
        name_len = raw[432];
        if (name_len > 0U && name_len <= 30U) {
            label.len = label.size = (int)name_len;
            label.data = raw + 433;
        }
    }
    status = fsw_strdup_coerce(&v->g.label, v->g.host_string_type, &label);
    if (status)
        return status;
    return fsw_dnode_create_root(v, (fsw_u64)v->ofs.root, &v->g.root);
}
static void ofs_free(struct fsw_ofs_volume *v) { (void)v; }
static fsw_status_t ofs_volume_stat(struct fsw_ofs_volume *v,
                                    struct fsw_volume_stat *stat)
{
    if (!v || !stat)
        return FSW_UNKNOWN_ERROR;
    stat->total_bytes = (fsw_u64)v->ofs.blocks * 512ULL;
    stat->free_bytes = 0ULL;
    return FSW_SUCCESS;
}
static fsw_status_t ofs_fill(struct fsw_ofs_volume *v,
                             struct fsw_ofs_dnode *node)
{
    if (!node || !v)
        return FSW_VOLUME_CORRUPTED;
    if (!node->initialized) {
        if (node->g.dnode_id != v->ofs.root)
            return FSW_VOLUME_CORRUPTED;
        node->entry = ifs_ofs_efi_root(&v->ofs);
        node->initialized = 1;
    }
    node->g.type = node->entry.type == IFS_OFS_EFI_DIRECTORY
                    ? FSW_DNODE_TYPE_DIR : FSW_DNODE_TYPE_FILE;
    node->g.size = node->entry.size;
    return FSW_SUCCESS;
}
static void ofs_dnode_free(struct fsw_ofs_volume *v,
                            struct fsw_ofs_dnode *node)
{
    (void)v; (void)node;
}
static fsw_status_t ofs_dnode_stat(struct fsw_ofs_volume *v,
                                   struct fsw_ofs_dnode *node,
                                   struct fsw_dnode_stat *stat)
{
    fsw_status_t status = ofs_fill(v, node);
    if (status)
        return status;
    if (!stat)
        return FSW_UNKNOWN_ERROR;
    stat->used_bytes = node->g.type == FSW_DNODE_TYPE_FILE
                        ? ((node->g.size + 487ULL) / 488ULL) * 512ULL : 512ULL;
    fsw_store_attr_posix(stat, (fsw_u16)(node->g.type == FSW_DNODE_TYPE_DIR
                           ? (S_IFDIR | 0555) : (S_IFREG | 0444)));
    return FSW_SUCCESS;
}
static fsw_status_t ofs_extent(struct fsw_ofs_volume *v,
                               struct fsw_ofs_dnode *node,
                               struct fsw_extent *extent)
{
    fsw_status_t status;
    fsw_u64 start;
    fsw_u32 to_read;
    void *bytes = NULL;
    if (!extent || ofs_fill(v, node) != FSW_SUCCESS ||
        node->g.type != FSW_DNODE_TYPE_FILE ||
        extent->log_start > 0xffffffffULL / 512ULL)
        return FSW_UNSUPPORTED;
    start = extent->log_start * 512ULL;
    if (start >= node->g.size)
        return FSW_NOT_FOUND;
    status = fsw_alloc(512, &bytes);
    if (status)
        return status;
    fsw_memzero(bytes, 512U);
    to_read = node->g.size - start < 512ULL
                ? (fsw_u32)(node->g.size - start) : 512U;
    if (ifs_ofs_efi_read(&v->ofs, &node->entry,
                         (uint32_t)start, bytes, to_read,
                         &node->sequential) != 0) {
        fsw_free(bytes);
        return FSW_VOLUME_CORRUPTED;
    }
    extent->type = FSW_EXTENT_TYPE_BUFFER;
    extent->log_count = 1U;
    extent->buffer = bytes;
    extent->phys_start = 0;
    return FSW_SUCCESS;
}
static fsw_status_t ofs_child(struct fsw_ofs_dnode *dir,
                              const IfsOfsEfiEntry *item,
                              struct fsw_string *name,
                              struct fsw_ofs_dnode **out)
{
    fsw_status_t status;
    struct fsw_ofs_dnode *child;
    status = fsw_dnode_create(dir, item->block,
              item->type == IFS_OFS_EFI_DIRECTORY
                ? FSW_DNODE_TYPE_DIR : FSW_DNODE_TYPE_FILE,
              name, &child);
    if (status)
        return status;
    if (child->initialized && (child->entry.block != item->block ||
                               child->entry.type != item->type ||
                               child->entry.size != item->size)) {
        fsw_dnode_release((struct fsw_dnode *)child);
        return FSW_VOLUME_CORRUPTED;
    }
    child->entry = *item;
    child->initialized = 1;
    *out = child;
    return FSW_SUCCESS;
}
static fsw_status_t ofs_lookup(struct fsw_ofs_volume *v,
                               struct fsw_ofs_dnode *dir,
                               struct fsw_string *name,
                               struct fsw_ofs_dnode **out)
{
    struct fsw_string bytes;
    IfsOfsEfiEntry item;
    fsw_status_t status;
    int found;
    if (!name || !out || ofs_fill(v, dir) != FSW_SUCCESS)
        return FSW_VOLUME_CORRUPTED;
    status = fsw_strdup_coerce(&bytes, FSW_STRING_TYPE_ISO88591, name);
    if (status)
        return status;
    found = ifs_ofs_efi_lookup(&v->ofs, &dir->entry,
                               (const uint8_t *)bytes.data,
                               (size_t)bytes.len, &item);
    if (found == 0)
        status = ofs_child(dir, &item, &bytes, out);
    else
        status = found == 1 ? FSW_NOT_FOUND : FSW_VOLUME_CORRUPTED;
    fsw_strfree(&bytes);
    return status;
}
static fsw_status_t ofs_next(struct fsw_ofs_volume *v,
                             struct fsw_ofs_dnode *dir,
                             struct fsw_shandle *handle,
                             struct fsw_ofs_dnode **out)
{
    IfsOfsEfiDirCursor cursor = {0};
    IfsOfsEfiEntry item;
    struct fsw_string label;
    uint8_t name[31];
    fsw_u64 i;
    fsw_status_t status;
    int found;
    if (!handle || !out || ofs_fill(v, dir) != FSW_SUCCESS ||
        handle->pos >= v->ofs.blocks)
        return FSW_NOT_FOUND;
    for (i = 0ULL; i <= handle->pos; ++i) {
        found = ifs_ofs_efi_next(&v->ofs, &dir->entry,
                                 &cursor, &item, name);
        if (found != 0)
            return found == 1 ? FSW_NOT_FOUND : FSW_VOLUME_CORRUPTED;
    }
    label.type = FSW_STRING_TYPE_ISO88591;
    label.len = 0;
    while (label.len < 30 && name[label.len] != 0U)
        ++label.len;
    label.size = label.len;
    label.data = name;
    status = ofs_child(dir, &item, &label, out);
    if (status == FSW_SUCCESS)
        ++handle->pos;
    return status;
}
static fsw_status_t ofs_readlink(struct fsw_ofs_volume *v,
                                 struct fsw_ofs_dnode *node,
                                 struct fsw_string *target)
{
    (void)v; (void)node; (void)target;
    return FSW_UNSUPPORTED;
}
