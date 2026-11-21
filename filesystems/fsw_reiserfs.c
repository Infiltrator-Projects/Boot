/*
 * ReiserFS firmware reader using tree keys, stat items and directory entries.
 * On-disk B-tree node contents, item boundaries and key ordering are untrusted.
 * The reader exposes file/dir operations to FSW; the mount must reject
 * unrecognised superblocks rather than displaying invented file contents.
 */

#include "fsw_reiserfs.h"

static fsw_status_t fsw_reiserfs_volume_mount(struct fsw_reiserfs_volume *vol);
static void         fsw_reiserfs_volume_free(struct fsw_reiserfs_volume *vol);
static fsw_status_t fsw_reiserfs_volume_stat(struct fsw_reiserfs_volume *vol, struct fsw_volume_stat *sb);

static fsw_status_t fsw_reiserfs_dnode_fill(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno);
static void         fsw_reiserfs_dnode_free(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno);
static fsw_status_t fsw_reiserfs_dnode_stat(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno,
                                        struct fsw_dnode_stat *sb);
static fsw_status_t fsw_reiserfs_get_extent(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno,
                                        struct fsw_extent *extent);

static fsw_status_t fsw_reiserfs_dir_lookup(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno,
                                        struct fsw_string *lookup_name, struct fsw_reiserfs_dnode **child_dno);
static fsw_status_t fsw_reiserfs_dir_read(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno,
                                      struct fsw_shandle *shand, struct fsw_reiserfs_dnode **child_dno);

static fsw_status_t fsw_reiserfs_readlink(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno,
                                      struct fsw_string *link);

static fsw_status_t fsw_reiserfs_item_search(struct fsw_reiserfs_volume *vol,
                                             fsw_u32 dir_id, fsw_u32 objectid, fsw_u64 offset,
                                             struct fsw_reiserfs_item *item);
static fsw_status_t fsw_reiserfs_item_next(struct fsw_reiserfs_volume *vol,
                                           struct fsw_reiserfs_item *item);
static void fsw_reiserfs_item_release(struct fsw_reiserfs_volume *vol,
                                      struct fsw_reiserfs_item *item);

struct fsw_fstype_table   FSW_FSTYPE_TABLE_NAME(reiserfs) = {
    { FSW_STRING_TYPE_ISO88591, 8, 8, "reiserfs" },
    sizeof(struct fsw_reiserfs_volume),
    sizeof(struct fsw_reiserfs_dnode),

    fsw_reiserfs_volume_mount,
    fsw_reiserfs_volume_free,
    fsw_reiserfs_volume_stat,
    fsw_reiserfs_dnode_fill,
    fsw_reiserfs_dnode_free,
    fsw_reiserfs_dnode_stat,
    fsw_reiserfs_get_extent,
    fsw_reiserfs_dir_lookup,
    fsw_reiserfs_dir_read,
    fsw_reiserfs_readlink,
};

static fsw_u32  superblock_offsets[3] = {
    REISERFS_DISK_OFFSET_IN_BYTES     >> REISERFS_SUPERBLOCK_BLOCKSIZEBITS,
    REISERFS_OLD_DISK_OFFSET_IN_BYTES >> REISERFS_SUPERBLOCK_BLOCKSIZEBITS,
    0
};

/*
 * Recognises supported ReiserFS superblocks and constructs the root dnode.
 * Feature-specific layout assumptions constrain volumes that can be read.
 */
static fsw_status_t fsw_reiserfs_volume_mount(struct fsw_reiserfs_volume *vol)
{
    fsw_status_t    status;
    void            *buffer;
    fsw_u32         blocksize;
    int             i;
    struct fsw_string s;

    status = fsw_alloc(sizeof(struct reiserfs_super_block), &vol->sb);
    if (status)
        return status;

    fsw_set_blocksize(vol, REISERFS_SUPERBLOCK_BLOCKSIZE, REISERFS_SUPERBLOCK_BLOCKSIZE);
    for (i = 0; superblock_offsets[i]; i++) {
        status = fsw_block_get(vol, superblock_offsets[i], 0, &buffer);
        if (status)
            return status;
        fsw_memcpy(vol->sb, buffer, sizeof(struct reiserfs_super_block));
        fsw_block_release(vol, superblock_offsets[i], buffer);

        if (fsw_memeq(vol->sb->s_v1.s_magic,
                      REISERFS_SUPER_MAGIC_STRING, 8)) {
            vol->version = REISERFS_VERSION_1;
            break;
        } else if (fsw_memeq(vol->sb->s_v1.s_magic,
                             REISER2FS_SUPER_MAGIC_STRING, 9)) {
            vol->version = REISERFS_VERSION_2;
            break;
        } else if (fsw_memeq(vol->sb->s_v1.s_magic,
                             REISER2FS_JR_SUPER_MAGIC_STRING, 9)) {
            vol->version = vol->sb->s_v1.s_version;
            if (vol->version == REISERFS_VERSION_1 || vol->version == REISERFS_VERSION_2)
                break;
        }
    }
    if (superblock_offsets[i] == 0)
        return FSW_UNSUPPORTED;

    if (vol->sb->s_v1.s_root_block == -1)    
        return FSW_VOLUME_CORRUPTED;

    blocksize = vol->sb->s_v1.s_blocksize;
    fsw_set_blocksize(vol, blocksize, blocksize);

    for (i = 0; i < 16; i++)
        if (vol->sb->s_label[i] == 0)
            break;
    s.type = FSW_STRING_TYPE_ISO88591;
    s.size = s.len = i;
    s.data = vol->sb->s_label;
    status = fsw_strdup_coerce(&vol->g.label, vol->g.host_string_type, &s);
    if (status)
        return status;

    status = fsw_dnode_create_root(vol, REISERFS_ROOT_OBJECTID, &vol->g.root);
    if (status)
        return status;
    vol->g.root->dir_id = REISERFS_ROOT_PARENT_OBJECTID;

    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_reiserfs_volume_mount: success, blocksize %d tree height %d\n"),
                   blocksize, vol->sb->s_v1.s_tree_height));

    return FSW_SUCCESS;
}

static void fsw_reiserfs_volume_free(struct fsw_reiserfs_volume *vol)
{
    if (vol->sb)
        fsw_free(vol->sb);
}

static fsw_status_t fsw_reiserfs_volume_stat(struct fsw_reiserfs_volume *vol, struct fsw_volume_stat *sb)
{
    sb->total_bytes = (fsw_u64)vol->sb->s_v1.s_block_count * vol->g.log_blocksize;
    sb->free_bytes  = (fsw_u64)vol->sb->s_v1.s_free_blocks * vol->g.log_blocksize;
    return FSW_SUCCESS;
}

static fsw_status_t fsw_reiserfs_dnode_fill(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno)
{
    fsw_status_t    status;
    fsw_u32         item_len, mode;
    struct fsw_reiserfs_item item;

    if (dno->sd_v1 || dno->sd_v2)
        return FSW_SUCCESS;

    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_reiserfs_dnode_fill: object %d/%d\n"), dno->dir_id, dno->g.dnode_id));

    status = fsw_reiserfs_item_search(vol, dno->dir_id, dno->g.dnode_id, 0, &item);
    if (status == FSW_NOT_FOUND) {
        FSW_MSG_ASSERT((FSW_MSGSTR("fsw_reiserfs_dnode_fill: cannot find stat_data for object %d/%d\n"),
                        dno->dir_id, dno->g.dnode_id));
        return FSW_VOLUME_CORRUPTED;
    }
    if (status)
        return status;
    if (item.item_offset != 0) {
        FSW_MSG_ASSERT((FSW_MSGSTR("fsw_reiserfs_dnode_fill: got item that's not stat_data\n")));
        fsw_reiserfs_item_release(vol, &item);
        return FSW_VOLUME_CORRUPTED;
    }
    item_len = item.ih.ih_item_len;

    if (item.ih.ih_version == KEY_FORMAT_3_5 && item_len == SD_V1_SIZE) {

        status = fsw_memdup((void **)&dno->sd_v1, item.item_data, item_len);
        fsw_reiserfs_item_release(vol, &item);
        if (status)
            return status;

        dno->g.size = dno->sd_v1->sd_size;
        mode = dno->sd_v1->sd_mode;

    } else if (item.ih.ih_version == KEY_FORMAT_3_6 && item_len == SD_V2_SIZE) {

        status = fsw_memdup((void **)&dno->sd_v2, item.item_data, item_len);
        fsw_reiserfs_item_release(vol, &item);
        if (status)
            return status;

        dno->g.size = dno->sd_v2->sd_size;
        mode = dno->sd_v2->sd_mode;

    } else {
        FSW_MSG_ASSERT((FSW_MSGSTR("fsw_reiserfs_dnode_fill: version %d(%d) and size %d(%d) not recognized for stat_data\n"),
                        item.ih.ih_version, KEY_FORMAT_3_6, item_len, SD_V2_SIZE));
        fsw_reiserfs_item_release(vol, &item);
        return FSW_VOLUME_CORRUPTED;
    }

    if (S_ISREG(mode))
        dno->g.type = FSW_DNODE_TYPE_FILE;
    else if (S_ISDIR(mode))
        dno->g.type = FSW_DNODE_TYPE_DIR;
    else if (S_ISLNK(mode))
        dno->g.type = FSW_DNODE_TYPE_SYMLINK;
    else
        dno->g.type = FSW_DNODE_TYPE_SPECIAL;

    return FSW_SUCCESS;
}

static void fsw_reiserfs_dnode_free(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno)
{
    if (dno->sd_v1)
        fsw_free(dno->sd_v1);
    if (dno->sd_v2)
        fsw_free(dno->sd_v2);
}

static fsw_status_t fsw_reiserfs_dnode_stat(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno,
                                            struct fsw_dnode_stat *sb)
{
    if (dno->sd_v1) {
        if (dno->g.type == FSW_DNODE_TYPE_SPECIAL)
            sb->used_bytes = 0;
        else
            sb->used_bytes = dno->sd_v1->u.sd_blocks * vol->g.log_blocksize;
        fsw_store_time_posix(sb, FSW_DNODE_STAT_CTIME, dno->sd_v1->sd_ctime);
        fsw_store_time_posix(sb, FSW_DNODE_STAT_ATIME, dno->sd_v1->sd_atime);
        fsw_store_time_posix(sb, FSW_DNODE_STAT_MTIME, dno->sd_v1->sd_mtime);
        fsw_store_attr_posix(sb, dno->sd_v1->sd_mode);
    } else if (dno->sd_v2) {
        sb->used_bytes = dno->sd_v2->sd_blocks * vol->g.log_blocksize;
        fsw_store_time_posix(sb, FSW_DNODE_STAT_CTIME, dno->sd_v2->sd_ctime);
        fsw_store_time_posix(sb, FSW_DNODE_STAT_ATIME, dno->sd_v2->sd_atime);
        fsw_store_time_posix(sb, FSW_DNODE_STAT_MTIME, dno->sd_v2->sd_mtime);
        fsw_store_attr_posix(sb, dno->sd_v2->sd_mode);
    }

    return FSW_SUCCESS;
}

/*
 * Maps tree-item file data into the FSW extent representation, preserving
 * holes and file size rather than assuming a contiguous physical region.
 */
static fsw_status_t fsw_reiserfs_get_extent(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno,
                                            struct fsw_extent *extent)
{
    fsw_status_t    status;
    fsw_u64         search_offset, intra_offset;
    struct fsw_reiserfs_item item;
    fsw_u32         intra_bno, nr_item;

    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_reiserfs_get_extent: mapping block %d of object %d/%d\n"),
                   extent->log_start, dno->dir_id, dno->g.dnode_id));

    extent->type = FSW_EXTENT_TYPE_SPARSE;
    extent->log_count = 1;
    /*
     * ReiserFS file-item keys count bytes starting at one. FSW requests
     * zero-based logical blocks. Adding one after the block-to-byte
     * conversion locates the first byte of the requested block.
     */
    search_offset = (fsw_u64)extent->log_start * vol->g.log_blocksize + 1;
    status = fsw_reiserfs_item_search(vol, dno->dir_id, dno->g.dnode_id, search_offset, &item);
    if (status)
        return status;
    if (item.item_offset == 0) {
        fsw_reiserfs_item_release(vol, &item);
        return FSW_SUCCESS;        
    }
    intra_offset = search_offset - item.item_offset;

    if (item.item_type == TYPE_INDIRECT || item.item_type == V1_INDIRECT_UNIQUENESS) {

        if (intra_offset & (vol->g.log_blocksize - 1)) {
            FSW_MSG_ASSERT((FSW_MSGSTR("fsw_reiserfs_get_extent: intra_offset not block-aligned for indirect block\n")));
            goto bail;
        }
        intra_bno = (fsw_u32)FSW_U64_DIV(intra_offset, vol->g.log_blocksize);
        nr_item = item.ih.ih_item_len / sizeof(fsw_u32);
        if (intra_bno >= nr_item) {
            FSW_MSG_ASSERT((FSW_MSGSTR("fsw_reiserfs_get_extent: indirect block too small\n")));
            goto bail;
        }
        extent->type = FSW_EXTENT_TYPE_PHYSBLOCK;
        extent->phys_start = ((fsw_u32 *)item.item_data)[intra_bno];

        fsw_reiserfs_item_release(vol, &item);
        return FSW_SUCCESS;

    } else if (item.item_type == TYPE_DIRECT || item.item_type == V1_DIRECT_UNIQUENESS) {

        if (intra_offset != 0) {
            FSW_MSG_ASSERT((FSW_MSGSTR("fsw_reiserfs_get_extent: intra_offset not aligned for direct block\n")));
            goto bail;
        }

        extent->type = FSW_EXTENT_TYPE_BUFFER;
        /*
         * A packed tail lives in the leaf item. Copy it into the returned FSW
         * buffer before releasing the tree block. The stream handle
         * subsequently owns that copy.
         */
        status = fsw_memdup(&extent->buffer, item.item_data, item.ih.ih_item_len);
        fsw_reiserfs_item_release(vol, &item);
        if (status)
            return status;

        return FSW_SUCCESS;

    }

bail:
    fsw_reiserfs_item_release(vol, &item);
    return FSW_VOLUME_CORRUPTED;

}

static fsw_status_t fsw_reiserfs_dir_lookup(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno,
                                            struct fsw_string *lookup_name, struct fsw_reiserfs_dnode **child_dno_out)
{
    fsw_status_t    status;
    struct fsw_reiserfs_item item;
    fsw_u32         nr_item, i, name_offset, next_name_offset, name_len;
    fsw_u32         child_dir_id;
    struct reiserfs_de_head *dhead;
    struct fsw_string entry_name;

    entry_name.type = FSW_STRING_TYPE_ISO88591;

    status = fsw_reiserfs_item_search(vol, dno->dir_id, dno->g.dnode_id, FIRST_ITEM_OFFSET, &item);
    if (status)
        return status;
    if (item.item_offset == 0) {
        fsw_reiserfs_item_release(vol, &item);
        return FSW_NOT_FOUND;        
    }

    for(;;) {

        dhead = (struct reiserfs_de_head *)item.item_data;
        nr_item = item.ih.u.ih_entry_count;
        next_name_offset = item.ih.ih_item_len;
        for (i = 0; i < nr_item; i++, dhead++, next_name_offset = name_offset) {

            name_offset = dhead->deh_location;
            name_len = next_name_offset - name_offset;
            while (name_len > 0 && item.item_data[name_offset + name_len - 1] == 0)
                name_len--;

            entry_name.len = entry_name.size = name_len;
            entry_name.data = item.item_data + name_offset;

            if (fsw_streq(lookup_name, &entry_name)) {

                status = fsw_dnode_create(dno, dhead->deh_objectid, FSW_DNODE_TYPE_UNKNOWN, &entry_name, child_dno_out);
                child_dir_id = dhead->deh_dir_id;
                fsw_reiserfs_item_release(vol, &item);
                if (status)
                    return status;
                (*child_dno_out)->dir_id = child_dir_id;

                return FSW_SUCCESS;
            }
        }

        status = fsw_reiserfs_item_next(vol, &item);
        if (status)
            return status;

    }
}

static fsw_status_t fsw_reiserfs_dir_read(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno,
                                          struct fsw_shandle *shand, struct fsw_reiserfs_dnode **child_dno_out)
{
    fsw_status_t    status;
    struct fsw_reiserfs_item item;
    fsw_u32         nr_item, i, name_offset, next_name_offset, name_len;
    fsw_u32         child_dir_id;
    struct reiserfs_de_head *dhead;
    struct fsw_string entry_name;

    if (shand->pos == 0)
        shand->pos = FIRST_ITEM_OFFSET;

    status = fsw_reiserfs_item_search(vol, dno->dir_id, dno->g.dnode_id, shand->pos, &item);
    if (status)
        return status;
    if (item.item_offset == 0) {
        fsw_reiserfs_item_release(vol, &item);
        return FSW_NOT_FOUND;        
    }

    for(;;) {

        dhead = (struct reiserfs_de_head *)item.item_data;
        nr_item = item.ih.u.ih_entry_count;
        for (i = 0; i < nr_item; i++, dhead++) {
            if (dhead->deh_offset < shand->pos)
                continue;   
            if (dhead->deh_offset == DOT_OFFSET || dhead->deh_offset == DOT_DOT_OFFSET)
                continue;   
            /*
             * Entry headers grow forwards and names are packed from the
             * opposite end of the item. The previous header's location, or
             * the item length for slot zero, bounds the current name.
             */
            name_offset = dhead->deh_location;
            if (i == 0)
                next_name_offset = item.ih.ih_item_len;
            else
                next_name_offset = dhead[-1].deh_location;
            name_len = next_name_offset - name_offset;
            while (name_len > 0 && item.item_data[name_offset + name_len - 1] == 0)
                name_len--;

            entry_name.type = FSW_STRING_TYPE_ISO88591;
            entry_name.len = entry_name.size = name_len;
            entry_name.data = item.item_data + name_offset;

            if (fsw_streq_cstr(&entry_name, ".reiserfs_priv"))
                continue;   
            /*
             * The next directory read resumes using the on-disk directory
             * offset. Records vary in length, so a byte count or an array
             * index would replay or skip entries.
             */
            shand->pos = dhead->deh_offset + 1;

            status = fsw_dnode_create(dno, dhead->deh_objectid, FSW_DNODE_TYPE_UNKNOWN, &entry_name, child_dno_out);
            child_dir_id = dhead->deh_dir_id;
            fsw_reiserfs_item_release(vol, &item);
            if (status)
                return status;
            (*child_dno_out)->dir_id = child_dir_id;

            return FSW_SUCCESS;
        }

        status = fsw_reiserfs_item_next(vol, &item);
        if (status)
            return status;

    }
}

static fsw_status_t fsw_reiserfs_readlink(struct fsw_reiserfs_volume *vol, struct fsw_reiserfs_dnode *dno,
                                          struct fsw_string *link_target)
{
    return fsw_dnode_readlink_data(dno, link_target);
}

static int fsw_reiserfs_compare_key(struct reiserfs_key *key,
                                    fsw_u32 dir_id, fsw_u32 objectid, fsw_u64 offset)
{
    fsw_u32 key_type;
    fsw_u64 key_offset;

    if (key->k_dir_id > dir_id)
        return FIRST_GREATER;
    if (key->k_dir_id < dir_id)
        return SECOND_GREATER;

    if (key->k_objectid > objectid)
        return FIRST_GREATER;
    if (key->k_objectid < objectid)
        return SECOND_GREATER;

    key_type = (fsw_u32)FSW_U64_SHR(key->u.k_offset_v2.v, 60);
    if (key_type != TYPE_DIRECT && key_type != TYPE_INDIRECT && key_type != TYPE_DIRENTRY) {

        key_offset = key->u.k_offset_v1.k_offset;
    } else {

        key_offset = key->u.k_offset_v2.v & (~0ULL >> 4);
    }
    if (key_offset > offset)
        return FIRST_GREATER;
    if (key_offset < offset)
        return SECOND_GREATER;
    return KEYS_IDENTICAL;
}

/*
 * Traverses B-tree items by filesystem key; correct comparisons and node
 * bounds are essential before interpreting directory or file data.
 */
static fsw_status_t fsw_reiserfs_item_search(struct fsw_reiserfs_volume *vol,
                                            fsw_u32 dir_id, fsw_u32 objectid, fsw_u64 offset,
                                            struct fsw_reiserfs_item *item)
{
    fsw_status_t    status;
    int             comp_result;
    fsw_u32         tree_bno, next_tree_bno, tree_level, nr_item, i;
    fsw_u8          *buffer;
    struct block_head *bhead;
    struct reiserfs_key *key;
    struct item_head *ihead;

    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_reiserfs_item_search: searching %d/%d/%lld\n"), dir_id, objectid, offset));

    item->valid = 0;
    item->block_bno = 0;

    tree_bno = vol->sb->s_v1.s_root_block;
    for (tree_level = vol->sb->s_v1.s_tree_height - 1; ; tree_level--) {

        status = fsw_block_get(vol, tree_bno, tree_level, (void **)&buffer);
        if (status)
            return status;
        bhead = (struct block_head *)buffer;
        if (bhead->blk_level != tree_level) {
            FSW_MSG_ASSERT((FSW_MSGSTR("fsw_reiserfs_item_search: tree block %d has not expected level %d\n"), tree_bno, tree_level));
            fsw_block_release(vol, tree_bno, buffer);
            return FSW_VOLUME_CORRUPTED;
        }
        nr_item = bhead->blk_nr_item;
        FSW_MSG_DEBUGV((FSW_MSGSTR("fsw_reiserfs_item_search: visiting block %d level %d items %d\n"), tree_bno, tree_level, nr_item));
        item->path_bno[tree_level] = tree_bno;

        if (tree_level == DISK_LEAF_NODE_LEVEL)
            break;

        key = (struct reiserfs_key *)(buffer + BLKH_SIZE);
        for (i = 0; i < nr_item; i++, key++) {
            if (fsw_reiserfs_compare_key(key, dir_id, objectid, offset) == FIRST_GREATER)
                break;
        }
        item->path_index[tree_level] = i;
        next_tree_bno = ((struct disk_child *)(buffer + BLKH_SIZE + nr_item * KEY_SIZE))[i].dc_block_number;
        fsw_block_release(vol, tree_bno, buffer);
        tree_bno = next_tree_bno;
    }

    ihead = (struct item_head *)(buffer + BLKH_SIZE);
    for (i = 0; i < nr_item; i++, ihead++) {
        comp_result = fsw_reiserfs_compare_key(&ihead->ih_key, dir_id, objectid, offset);
        if (comp_result == KEYS_IDENTICAL)
            break;
        if (comp_result == FIRST_GREATER) {

            if (i == 0) {
                fsw_block_release(vol, tree_bno, buffer);
                return FSW_NOT_FOUND;
            }
            i--, ihead--;
            break;
        }
    }
    if (i >= nr_item) {

        i--, ihead--;
    }
    item->path_index[tree_level] = i;

    if (ihead->ih_key.k_dir_id != dir_id || ihead->ih_key.k_objectid != objectid) {
        fsw_block_release(vol, tree_bno, buffer);
        return FSW_NOT_FOUND;    
    }

    fsw_memcpy(&item->ih, ihead, sizeof(struct item_head));
    item->item_type = (fsw_u32)FSW_U64_SHR(ihead->ih_key.u.k_offset_v2.v, 60);
    if (item->item_type != TYPE_DIRECT &&
        item->item_type != TYPE_INDIRECT &&
        item->item_type != TYPE_DIRENTRY) {

        item->item_type = ihead->ih_key.u.k_offset_v1.k_uniqueness;
        item->item_offset = ihead->ih_key.u.k_offset_v1.k_offset;
    } else {

        item->item_offset = ihead->ih_key.u.k_offset_v2.v & (~0ULL >> 4);
    }
    item->item_data = buffer + ihead->ih_item_location;
    item->valid = 1;

    item->block_bno = tree_bno;
    item->block_buffer = buffer;

    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_reiserfs_item_search: found %d/%d/%lld (%d)\n"),
                   ihead->ih_key.k_dir_id, ihead->ih_key.k_objectid, item->item_offset, item->item_type));
    return FSW_SUCCESS;
}

/*
 * Advances an item iterator; node transitions must preserve caller-visible
 * state and release previous block references when moving between nodes.
 */
static fsw_status_t fsw_reiserfs_item_next(struct fsw_reiserfs_volume *vol,
                                          struct fsw_reiserfs_item *item)
{
    fsw_status_t    status;
    fsw_u32         dir_id, objectid;
    fsw_u32         tree_bno, next_tree_bno, tree_level, nr_item, nr_ptr_item;
    fsw_u8          *buffer;
    struct block_head *bhead;
    struct item_head *ihead;

    if (!item->valid)
        return FSW_NOT_FOUND;
    fsw_reiserfs_item_release(vol, item);    

    dir_id = item->ih.ih_key.k_dir_id;
    objectid = item->ih.ih_key.k_objectid;

    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_reiserfs_item_next: next for %d/%d/%lld\n"), dir_id, objectid, item->item_offset));

    for (tree_level = DISK_LEAF_NODE_LEVEL; tree_level < vol->sb->s_v1.s_tree_height; tree_level++) {

        tree_bno = item->path_bno[tree_level];
        status = fsw_block_get(vol, tree_bno, tree_level, (void **)&buffer);
        if (status)
            return status;
        bhead = (struct block_head *)buffer;
        if (bhead->blk_level != tree_level) {
            FSW_MSG_ASSERT((FSW_MSGSTR("fsw_reiserfs_item_next: tree block %d has not expected level %d\n"), tree_bno, tree_level));
            fsw_block_release(vol, tree_bno, buffer);
            return FSW_VOLUME_CORRUPTED;
        }
        nr_item = bhead->blk_nr_item;
        FSW_MSG_DEBUGV((FSW_MSGSTR("fsw_reiserfs_item_next: visiting block %d level %d items %d\n"), tree_bno, tree_level, nr_item));

        nr_ptr_item = nr_item + ((tree_level > DISK_LEAF_NODE_LEVEL) ? 1 : 0);   
        item->path_index[tree_level]++;
        if (item->path_index[tree_level] >= nr_ptr_item) {
            item->path_index[tree_level] = 0;
            fsw_block_release(vol, tree_bno, buffer);
            continue;   
        }

        while (tree_level > DISK_LEAF_NODE_LEVEL) {

            next_tree_bno = ((struct disk_child *)(buffer + BLKH_SIZE + nr_item * KEY_SIZE))[item->path_index[tree_level]].dc_block_number;
            fsw_block_release(vol, tree_bno, buffer);
            tree_bno = next_tree_bno;
            tree_level--;

            status = fsw_block_get(vol, tree_bno, tree_level, (void **)&buffer);
            if (status)
                return status;
            bhead = (struct block_head *)buffer;
            if (bhead->blk_level != tree_level) {
                FSW_MSG_ASSERT((FSW_MSGSTR("fsw_reiserfs_item_next: tree block %d has not expected level %d\n"), tree_bno, tree_level));
                fsw_block_release(vol, tree_bno, buffer);
                return FSW_VOLUME_CORRUPTED;
            }
            nr_item = bhead->blk_nr_item;
            FSW_MSG_DEBUGV((FSW_MSGSTR("fsw_reiserfs_item_next: visiting block %d level %d items %d\n"), tree_bno, tree_level, nr_item));
            item->path_bno[tree_level] = tree_bno;
        }

        ihead = ((struct item_head *)(buffer + BLKH_SIZE)) + item->path_index[tree_level];

        if (ihead->ih_key.k_dir_id != dir_id || ihead->ih_key.k_objectid != objectid) {
            fsw_block_release(vol, tree_bno, buffer);
            return FSW_NOT_FOUND;    
        }

        fsw_memcpy(&item->ih, ihead, sizeof(struct item_head));
        item->item_type = (fsw_u32)FSW_U64_SHR(ihead->ih_key.u.k_offset_v2.v, 60);
        if (item->item_type != TYPE_DIRECT &&
            item->item_type != TYPE_INDIRECT &&
            item->item_type != TYPE_DIRENTRY) {

            item->item_type = ihead->ih_key.u.k_offset_v1.k_uniqueness;
            item->item_offset = ihead->ih_key.u.k_offset_v1.k_offset;
        } else {

            item->item_offset = ihead->ih_key.u.k_offset_v2.v & (~0ULL >> 4);
        }
        item->item_data = buffer + ihead->ih_item_location;
        item->valid = 1;

        item->block_bno = tree_bno;
        item->block_buffer = buffer;

        FSW_MSG_DEBUG((FSW_MSGSTR("fsw_reiserfs_item_next: found %d/%d/%lld (%d)\n"),
                       ihead->ih_key.k_dir_id, ihead->ih_key.k_objectid, item->item_offset, item->item_type));
        return FSW_SUCCESS;
    }

    return FSW_NOT_FOUND;
}

static void fsw_reiserfs_item_release(struct fsw_reiserfs_volume *vol,
                                      struct fsw_reiserfs_item *item)
{
    if (!item->valid)
        return;

    if (item->block_bno > 0) {
        fsw_block_release(vol, item->block_bno, item->block_buffer);
        item->block_bno = 0;
    }
}
