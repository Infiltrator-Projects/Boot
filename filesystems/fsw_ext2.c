/*
 * EXT2-family read-only filesystem backend for the firmware access layer.
 * A superblock's magic/revision/feature bits determine whether this reader
 * can interpret the volume. Inode group tables, direct/indirect addressing,
 * directory records and symlink targets originate from untrusted disk bytes.
 */

#include "fsw_ext2.h"

static fsw_status_t fsw_ext2_volume_mount(struct fsw_ext2_volume *vol);
static void         fsw_ext2_volume_free(struct fsw_ext2_volume *vol);
static fsw_status_t fsw_ext2_volume_stat(struct fsw_ext2_volume *vol, struct fsw_volume_stat *sb);

static fsw_status_t fsw_ext2_dnode_fill(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno);
static void         fsw_ext2_dnode_free(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno);
static fsw_status_t fsw_ext2_dnode_stat(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno,
                                        struct fsw_dnode_stat *sb);
static fsw_status_t fsw_ext2_get_extent(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno,
                                        struct fsw_extent *extent);

static fsw_status_t fsw_ext2_dir_lookup(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno,
                                        struct fsw_string *lookup_name, struct fsw_ext2_dnode **child_dno);
static fsw_status_t fsw_ext2_dir_read(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno,
                                      struct fsw_shandle *shand, struct fsw_ext2_dnode **child_dno);
static fsw_status_t fsw_ext2_read_dentry(struct fsw_shandle *shand, struct ext2_dir_entry *entry);

static fsw_status_t fsw_ext2_readlink(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno,
                                      struct fsw_string *link);

struct fsw_fstype_table   FSW_FSTYPE_TABLE_NAME(ext2) = {
    { FSW_STRING_TYPE_ISO88591, 4, 4, "ext2" },
    sizeof(struct fsw_ext2_volume),
    sizeof(struct fsw_ext2_dnode),

    fsw_ext2_volume_mount,
    fsw_ext2_volume_free,
    fsw_ext2_volume_stat,
    fsw_ext2_dnode_fill,
    fsw_ext2_dnode_free,
    fsw_ext2_dnode_stat,
    fsw_ext2_get_extent,
    fsw_ext2_dir_lookup,
    fsw_ext2_dir_read,
    fsw_ext2_readlink,
};

/*
 * Checks EXT2 superblock signature/revision and incompatible features,
 * then obtains inode-group table locations and creates the root dnode.
 * AUDIT: s_inodes_per_group is used as a divisor; malformed zero geometry
 * needs an explicit validity guard before this calculation.
 */
static fsw_status_t fsw_ext2_volume_mount(struct fsw_ext2_volume *vol)
{
    fsw_status_t    status;
    void            *buffer;
    fsw_u32         blocksize;
    fsw_u32         groupcnt, groupno, gdesc_per_block, gdesc_bno, gdesc_index;
    struct ext2_group_desc *gdesc;
    int             i;
    struct fsw_string s;

    status = fsw_alloc(sizeof(struct ext2_super_block), &vol->sb);
    if (status)
        return status;

    fsw_set_blocksize(vol, EXT2_SUPERBLOCK_BLOCKSIZE, EXT2_SUPERBLOCK_BLOCKSIZE);
    status = fsw_block_get(vol, EXT2_SUPERBLOCK_BLOCKNO, 0, &buffer);
    if (status)
        return status;
    fsw_memcpy(vol->sb, buffer, sizeof(struct ext2_super_block));
    fsw_block_release(vol, EXT2_SUPERBLOCK_BLOCKNO, buffer);

    if (vol->sb->s_magic != EXT2_SUPER_MAGIC)
        return FSW_UNSUPPORTED;
    if (vol->sb->s_rev_level != EXT2_GOOD_OLD_REV &&
        vol->sb->s_rev_level != EXT2_DYNAMIC_REV)
        return FSW_UNSUPPORTED;
    if (vol->sb->s_rev_level == EXT2_DYNAMIC_REV &&
        (vol->sb->s_feature_incompat & ~(EXT2_FEATURE_INCOMPAT_FILETYPE | EXT3_FEATURE_INCOMPAT_RECOVER)))
        return FSW_UNSUPPORTED;

    blocksize = EXT2_BLOCK_SIZE(vol->sb);
    fsw_set_blocksize(vol, blocksize, blocksize);

    vol->ind_bcnt = EXT2_ADDR_PER_BLOCK(vol->sb);
    vol->dind_bcnt = vol->ind_bcnt * vol->ind_bcnt;
    vol->inode_size = EXT2_INODE_SIZE(vol->sb);

    for (i = 0; i < 16; i++)
        if (vol->sb->s_volume_name[i] == 0)
            break;
    s.type = FSW_STRING_TYPE_ISO88591;
    s.size = s.len = i;
    s.data = vol->sb->s_volume_name;
    status = fsw_strdup_coerce(&vol->g.label, vol->g.host_string_type, &s);
    if (status)
        return status;

    groupcnt = ((vol->sb->s_inodes_count - 2) / vol->sb->s_inodes_per_group) + 1;
    gdesc_per_block = (vol->g.phys_blocksize / sizeof(struct ext2_group_desc));

    status = fsw_alloc(sizeof(fsw_u32) * groupcnt, &vol->inotab_bno);
    if (status)
        return status;
    for (groupno = 0; groupno < groupcnt; groupno++) {

        gdesc_bno = (vol->sb->s_first_data_block + 1) + groupno / gdesc_per_block;
        gdesc_index = groupno % gdesc_per_block;
        status = fsw_block_get(vol, gdesc_bno, 1, (void **)&buffer);
        if (status)
            return status;
        gdesc = ((struct ext2_group_desc *)(buffer)) + gdesc_index;
        vol->inotab_bno[groupno] = gdesc->bg_inode_table;
        fsw_block_release(vol, gdesc_bno, buffer);
    }

    status = fsw_dnode_create_root(vol, EXT2_ROOT_INO, &vol->g.root);
    if (status)
        return status;

    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext2_volume_mount: success, blocksize %d\n"), blocksize));

    return FSW_SUCCESS;
}

static void fsw_ext2_volume_free(struct fsw_ext2_volume *vol)
{
    if (vol->sb)
        fsw_free(vol->sb);
    if (vol->inotab_bno)
        fsw_free(vol->inotab_bno);
}

static fsw_status_t fsw_ext2_volume_stat(struct fsw_ext2_volume *vol, struct fsw_volume_stat *sb)
{
    sb->total_bytes = (fsw_u64)vol->sb->s_blocks_count      * vol->g.log_blocksize;
    sb->free_bytes  = (fsw_u64)vol->sb->s_free_blocks_count * vol->g.log_blocksize;
    return FSW_SUCCESS;
}

static fsw_status_t fsw_ext2_dnode_fill(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno)
{
    fsw_status_t    status;
    fsw_u32         groupno, ino_in_group, ino_bno, ino_index;
    fsw_u8          *buffer;

    if (dno->raw)
        return FSW_SUCCESS;

    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext2_dnode_fill: inode %d\n"), dno->g.dnode_id));

    groupno = (fsw_u32) (dno->g.dnode_id - 1) / vol->sb->s_inodes_per_group;
    ino_in_group = (fsw_u32) (dno->g.dnode_id - 1) % vol->sb->s_inodes_per_group;
    ino_bno = vol->inotab_bno[groupno] +
        ino_in_group / (vol->g.phys_blocksize / vol->inode_size);
    ino_index = ino_in_group % (vol->g.phys_blocksize / vol->inode_size);
    status = fsw_block_get(vol, ino_bno, 2, (void **)&buffer);
    if (status)
        return status;

    status = fsw_memdup((void **)&dno->raw, buffer + ino_index * vol->inode_size, vol->inode_size);
    fsw_block_release(vol, ino_bno, buffer);
    if (status)
        return status;

    dno->g.size = dno->raw->i_size;

    if (S_ISREG(dno->raw->i_mode))
        dno->g.type = FSW_DNODE_TYPE_FILE;
    else if (S_ISDIR(dno->raw->i_mode))
        dno->g.type = FSW_DNODE_TYPE_DIR;
    else if (S_ISLNK(dno->raw->i_mode))
        dno->g.type = FSW_DNODE_TYPE_SYMLINK;
    else
        dno->g.type = FSW_DNODE_TYPE_SPECIAL;

    return FSW_SUCCESS;
}

static void fsw_ext2_dnode_free(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno)
{
    if (dno->raw)
        fsw_free(dno->raw);
}

static fsw_status_t fsw_ext2_dnode_stat(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno,
                                        struct fsw_dnode_stat *sb)
{
    sb->used_bytes = dno->raw->i_blocks * 512;    
    fsw_store_time_posix(sb, FSW_DNODE_STAT_CTIME, dno->raw->i_ctime);
    fsw_store_time_posix(sb, FSW_DNODE_STAT_ATIME, dno->raw->i_atime);
    fsw_store_time_posix(sb, FSW_DNODE_STAT_MTIME, dno->raw->i_mtime);
    fsw_store_attr_posix(sb, dno->raw->i_mode);

    return FSW_SUCCESS;
}

/*
 * Maps an inode's logical file blocks through its direct and indirect
 * addressing. A valid extent must stay within the underlying volume.
 */
static fsw_status_t fsw_ext2_get_extent(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno,
                                        struct fsw_extent *extent)
{
    fsw_status_t    status;
    fsw_u32         bno, release_bno, buf_bcnt, file_bcnt;
    fsw_u32         *buffer;
    int             path[5], i;

    extent->type = FSW_EXTENT_TYPE_PHYSBLOCK;
    extent->log_count = 1;
    bno = extent->log_start;
    /*
     * After subtracting the direct region, a pointer block contributes
     * ind_bcnt entries. Quotient and remainder select successive levels of
     * indirection. The negative terminator tells the walker when the current
     * pointer names data.
     */
    if (bno < EXT2_NDIR_BLOCKS) {
        path[0] = bno;
        path[1] = -1;
    } else {
        bno -= EXT2_NDIR_BLOCKS;

        if (bno < vol->ind_bcnt) {
            path[0] = EXT2_IND_BLOCK;
            path[1] = bno;
            path[2] = -1;
        } else {
            bno -= vol->ind_bcnt;

            if (bno < vol->dind_bcnt) {
                path[0] = EXT2_DIND_BLOCK;
                path[1] = bno / vol->ind_bcnt;
                path[2] = bno % vol->ind_bcnt;
                path[3] = -1;
            } else {
                bno -= vol->dind_bcnt;

                path[0] = EXT2_TIND_BLOCK;
                path[1] = bno / vol->dind_bcnt;
                path[2] = (bno / vol->ind_bcnt) % vol->ind_bcnt;
                path[3] = bno % vol->ind_bcnt;
                path[4] = -1;
            }
        }
    }

    /*
     * The inode owns the first pointer array. Subsequent arrays are pinned
     * cache blocks with a release_bno. A zero pointer at any level maps the
     * requested block to a hole without reading block zero.
     */
    buffer = dno->raw->i_block;
    buf_bcnt = EXT2_NDIR_BLOCKS;
    release_bno = 0;
    for (i = 0; ; i++) {
        bno = buffer[path[i]];
        if (bno == 0) {
            extent->type = FSW_EXTENT_TYPE_SPARSE;
            if (release_bno)
                fsw_block_release(vol, release_bno, buffer);
            return FSW_SUCCESS;
        }
        if (path[i+1] < 0)
            break;

        if (release_bno)
            fsw_block_release(vol, release_bno, buffer);
        status = fsw_block_get(vol, bno, 1, (void **)&buffer);
        if (status)
            return status;
        release_bno = bno;
        buf_bcnt = vol->ind_bcnt;
    }
    extent->phys_start = bno;

    file_bcnt = (fsw_u32)((dno->g.size + vol->g.log_blocksize - 1) & (vol->g.log_blocksize - 1));
    while (path[i]           + extent->log_count < buf_bcnt &&     
           extent->log_start + extent->log_count < file_bcnt) {    
        if (buffer[path[i] + extent->log_count] == buffer[path[i] + extent->log_count - 1] + 1)
            extent->log_count++;
        else
            break;
    }

    if (release_bno)
        fsw_block_release(vol, release_bno, buffer);
    return FSW_SUCCESS;
}

static fsw_status_t fsw_ext2_dir_lookup(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno,
                                        struct fsw_string *lookup_name, struct fsw_ext2_dnode **child_dno_out)
{
    fsw_status_t    status;
    struct fsw_shandle shand;
    fsw_u32         child_ino;
    struct ext2_dir_entry entry;
    struct fsw_string entry_name;

    entry_name.type = FSW_STRING_TYPE_ISO88591;

    status = fsw_shandle_open(dno, &shand);
    if (status)
        return status;

    child_ino = 0;
    while (child_ino == 0) {

        status = fsw_ext2_read_dentry(&shand, &entry);
        if (status)
            goto errorexit;
        if (entry.inode == 0) {

            status = FSW_NOT_FOUND;
            goto errorexit;
        }

        entry_name.len = entry_name.size = entry.name_len;
        entry_name.data = entry.name;
        if (fsw_streq(lookup_name, &entry_name)) {
            child_ino = entry.inode;
            break;
        }
    }

    status = fsw_dnode_create(dno, child_ino, FSW_DNODE_TYPE_UNKNOWN, &entry_name, child_dno_out);

errorexit:
    fsw_shandle_close(&shand);
    return status;
}

/*
 * Reads filesystem directory records into dnodes. Record length and name
 * bounds are disk-controlled and must be checked before iteration.
 */
static fsw_status_t fsw_ext2_dir_read(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno,
                                      struct fsw_shandle *shand, struct fsw_ext2_dnode **child_dno_out)
{
    fsw_status_t    status;
    struct ext2_dir_entry entry;
    struct fsw_string entry_name;

    while (1) {

        status = fsw_ext2_read_dentry(shand, &entry);
        if (status)
            return status;
        if (entry.inode == 0)    
            return FSW_NOT_FOUND;

        if ((entry.name_len == 1 && entry.name[0] == '.') ||
            (entry.name_len == 2 && entry.name[0] == '.' && entry.name[1] == '.'))
            continue;
        break;
    }

    entry_name.type = FSW_STRING_TYPE_ISO88591;
    entry_name.len = entry_name.size = entry.name_len;
    entry_name.data = entry.name;

    status = fsw_dnode_create(dno, entry.inode, FSW_DNODE_TYPE_UNKNOWN, &entry_name, child_dno_out);

    return status;
}

static fsw_status_t fsw_ext2_read_dentry(struct fsw_shandle *shand, struct ext2_dir_entry *entry)
{
    fsw_status_t    status;
    fsw_u32         buffer_size;

    while (1) {

        buffer_size = 8;
        status = fsw_shandle_read(shand, &buffer_size, entry);
        if (status)
            return status;

        if (buffer_size < 8 || entry->rec_len == 0) {

            entry->inode = 0;
            return FSW_SUCCESS;
        }
        if (entry->rec_len < 8)
            return FSW_VOLUME_CORRUPTED;
        if (entry->inode != 0) {

            if (entry->rec_len < 8 + entry->name_len)
                return FSW_VOLUME_CORRUPTED;
            break;
        }

        /*
         * The eight-byte header has already moved the stream cursor. A
         * deleted entry still occupies its full record on disk, so advancing
         * by its name length would land the next read inside that record.
         */
        shand->pos += entry->rec_len - 8;
    }

    buffer_size = entry->name_len;
    status = fsw_shandle_read(shand, &buffer_size, entry->name);
    if (status)
        return status;
    if (buffer_size < entry->name_len)
        return FSW_VOLUME_CORRUPTED;

    shand->pos += entry->rec_len - (8 + entry->name_len);

    return FSW_SUCCESS;
}

/*
 * Extracts symlink content from inode or data blocks without confusing
 * link targets with already resolved, trusted loader paths.
 */
static fsw_status_t fsw_ext2_readlink(struct fsw_ext2_volume *vol, struct fsw_ext2_dnode *dno,
                                      struct fsw_string *link_target)
{
    fsw_status_t    status;
    int             ea_blocks;
    struct fsw_string s;

    if ((dno->g.size > FSW_PATH_MAX) || (dno->g.size > sizeof(dno->raw->i_block)))
        return FSW_VOLUME_CORRUPTED;

    ea_blocks = dno->raw->i_file_acl ? (vol->g.log_blocksize >> 9) : 0;
    /*
     * An external attribute block can account for allocated sectors even when
     * the link text lives inside i_block. Discount those sectors before
     * choosing between the inline bytes and the file-data path.
     */
    if (dno->raw->i_blocks - ea_blocks == 0) {

        s.type = FSW_STRING_TYPE_ISO88591;
        s.size = s.len = (int)dno->g.size;
        s.data = dno->raw->i_block;
        status = fsw_strdup_coerce(link_target, vol->g.host_string_type, &s);
    } else {

        status = fsw_dnode_readlink_data(dno, link_target);
    }

    return status;
}
