/*
 * EXT4-format firmware reader using FSW volume, dnode and extent APIs.
 * Superblock feature negotiation is mandatory: an EXT4 signature is not enough
 * for a reader to understand every on-disk feature. Inodes may use either
 * extent trees or direct/indirect block addressing, with distinct risks.
 */

#include "fsw_ext4.h"

static fsw_status_t fsw_ext4_volume_mount(struct fsw_ext4_volume *vol);
static void         fsw_ext4_volume_free(struct fsw_ext4_volume *vol);
static fsw_status_t fsw_ext4_volume_stat(struct fsw_ext4_volume *vol, struct fsw_volume_stat *sb);

static fsw_status_t fsw_ext4_dnode_fill(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno);
static void         fsw_ext4_dnode_free(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno);
static fsw_status_t fsw_ext4_dnode_stat(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                        struct fsw_dnode_stat *sb);
static fsw_status_t fsw_ext4_get_extent(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                        struct fsw_extent *extent);
static fsw_status_t fsw_ext4_get_by_blkaddr(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                        struct fsw_extent *extent);
static fsw_status_t fsw_ext4_get_by_extent(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                        struct fsw_extent *extent);

static fsw_status_t fsw_ext4_dir_lookup(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                        struct fsw_string *lookup_name, struct fsw_ext4_dnode **child_dno);
static fsw_status_t fsw_ext4_dir_read(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                      struct fsw_shandle *shand, struct fsw_ext4_dnode **child_dno);
static fsw_status_t fsw_ext4_read_dentry(struct fsw_shandle *shand, struct ext4_dir_entry *entry);

static fsw_status_t fsw_ext4_readlink(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                      struct fsw_string *link);

struct fsw_fstype_table   FSW_FSTYPE_TABLE_NAME(ext4) = {
    { FSW_STRING_TYPE_ISO88591, 4, 4, "ext4" },
    sizeof(struct fsw_ext4_volume),
    sizeof(struct fsw_ext4_dnode),

    fsw_ext4_volume_mount,
    fsw_ext4_volume_free,
    fsw_ext4_volume_stat,
    fsw_ext4_dnode_fill,
    fsw_ext4_dnode_free,
    fsw_ext4_dnode_stat,
    fsw_ext4_get_extent,
    fsw_ext4_dir_lookup,
    fsw_ext4_dir_read,
    fsw_ext4_readlink,
};

static __inline int test_root(fsw_u32 a, int b)
{
        fsw_u32 num = b;

        while (a > num)
                num *= b;
        return num == a;
}

static int fsw_ext4_group_sparse(fsw_u32 group)
{
        if (group <= 1)
                return 1;
        if (!(group & 1))
                return 0;
        return (test_root(group, 7) || test_root(group, 5) ||
                test_root(group, 3));
}

static __inline fsw_u64
fsw_ext4_group_first_block_no(struct ext4_super_block *sb, fsw_u32 group_no)
{
        return group_no * (fsw_u64)EXT4_BLOCKS_PER_GROUP(sb) +
                sb->s_first_data_block;
}

/**
 * Probe the superblock using its fixed initial block units, then switch FSW to
 * the filesystem's block size. Cache inode-table start blocks from the group
 * descriptors. Descriptor size determines whether to combine high and low
 * block-number fields.
 *
 * The feature test admits a filesystem marked for recovery and emits a debug
 * message. The read-only mount does not replay its journal. Group count is
 * computed from s_blocks_count_lo even when 64-bit descriptors are admitted.
 */
static fsw_status_t fsw_ext4_volume_mount(struct fsw_ext4_volume *vol)
{
    fsw_status_t    status;
    void            *buffer;
    fsw_u32         blocksize;
    fsw_u32         groupcnt, groupno, gdesc_per_block, gdesc_index, metabg_of_gdesc;
    fsw_u64         gdesc_bno;
    struct ext4_group_desc *gdesc;
    int             i;
    struct fsw_string s;

    status = fsw_alloc(sizeof(struct ext4_super_block), &vol->sb);
    if (status)
        return status;

    fsw_set_blocksize(vol, EXT4_SUPERBLOCK_BLOCKSIZE, EXT4_SUPERBLOCK_BLOCKSIZE);
    status = fsw_block_get(vol, EXT4_SUPERBLOCK_BLOCKNO, 0, &buffer);
    if (status)
        return status;
    fsw_memcpy(vol->sb, buffer, sizeof(struct ext4_super_block));
    fsw_block_release(vol, EXT4_SUPERBLOCK_BLOCKNO, buffer);

    if (vol->sb->s_magic != EXT4_SUPER_MAGIC)
        return FSW_UNSUPPORTED;
    if (vol->sb->s_rev_level != EXT4_GOOD_OLD_REV &&
        vol->sb->s_rev_level != EXT4_DYNAMIC_REV)
        return FSW_UNSUPPORTED;

    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext4_volume_mount: Incompat flag %x\n"), vol->sb->s_feature_incompat));

    if (vol->sb->s_rev_level == EXT4_DYNAMIC_REV &&
        (vol->sb->s_feature_incompat & ~(EXT4_FEATURE_INCOMPAT_FILETYPE | EXT4_FEATURE_INCOMPAT_RECOVER |
                                         EXT4_FEATURE_INCOMPAT_EXTENTS | EXT4_FEATURE_INCOMPAT_FLEX_BG |
                                         EXT4_FEATURE_INCOMPAT_64BIT | EXT4_FEATURE_INCOMPAT_META_BG |
                                         EXT4_FEATURE_INCOMPAT_ENCRYPT | EXT4_FEATURE_INCOMPAT_BG_USE_META_CSUM)))
        return FSW_UNSUPPORTED;
    /*
     * RECOVER is admitted by the feature mask. This branch logs the dirty
     * journal and continues with the metadata currently on disk. Nothing here
     * replays transactions before the inode tables are read.
     */
    if (vol->sb->s_rev_level == EXT4_DYNAMIC_REV &&
        (vol->sb->s_feature_incompat & EXT4_FEATURE_INCOMPAT_RECOVER))
    {
        FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext4_volume_mount: This ext3 file system needs recovery\n")));

    }

    blocksize = EXT4_BLOCK_SIZE(vol->sb);
    if (blocksize < EXT4_MIN_BLOCK_SIZE || blocksize > EXT4_MAX_BLOCK_SIZE)
        return FSW_UNSUPPORTED;

    fsw_set_blocksize(vol, blocksize, blocksize);

    vol->ind_bcnt = EXT4_ADDR_PER_BLOCK(vol->sb);
    vol->dind_bcnt = vol->ind_bcnt * vol->ind_bcnt;
    vol->inode_size = vol->sb->s_inode_size; 

    for (i = 0; i < 16; i++)
        if (vol->sb->s_volume_name[i] == 0)
            break;
    s.type = FSW_STRING_TYPE_ISO88591;
    s.size = s.len = i;
    s.data = vol->sb->s_volume_name;
    status = fsw_strdup_coerce(&vol->g.label, vol->g.host_string_type, &s);
    if (status)
        return status;

    if (!(vol->sb->s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT)) {

        vol->sb->s_desc_size = EXT4_MIN_DESC_SIZE;
    }

    groupcnt = (vol->sb->s_blocks_count_lo - vol->sb->s_first_data_block +
                vol->sb->s_blocks_per_group - 1) / vol->sb->s_blocks_per_group;

    gdesc_per_block = EXT4_DESC_PER_BLOCK(vol->sb);

    status = fsw_alloc(sizeof(fsw_u64) * groupcnt, &vol->inotab_bno);
    if (status)
        return status;
    /*
     * META_BG changes where the descriptor block lives. Within that block the
     * group number still selects a slot modulo gdesc_per_block. Whether a
     * backup superblock occupies the first block of the meta group changes
     * the physical address by one.
     */
    for (groupno = 0; groupno < groupcnt; groupno++) {

        if(vol->sb->s_feature_incompat & EXT4_FEATURE_INCOMPAT_META_BG && groupno >= vol->sb->s_first_meta_bg)
        {

            metabg_of_gdesc = (fsw_u32)(groupno / gdesc_per_block) * gdesc_per_block;
            gdesc_bno = fsw_ext4_group_first_block_no(vol->sb, metabg_of_gdesc);

            if(!(vol->sb->s_feature_ro_compat & EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER) || fsw_ext4_group_sparse(metabg_of_gdesc))
                gdesc_bno += 1;
        }
        else
        {

            gdesc_bno = (vol->sb->s_first_data_block + 1) + groupno / gdesc_per_block;
        }
        gdesc_index = groupno % gdesc_per_block;

        status = fsw_block_get(vol, gdesc_bno, 1, (void **)&buffer);
        if (status)
            return status;

        gdesc = (struct ext4_group_desc *)((char *)buffer + gdesc_index * vol->sb->s_desc_size);
        vol->inotab_bno[groupno] = gdesc->bg_inode_table_lo;
        if (vol->sb->s_desc_size >= EXT4_MIN_DESC_SIZE_64BIT)
            vol->inotab_bno[groupno] |= (fsw_u64)gdesc->bg_inode_table_hi << 32;

        fsw_block_release(vol, gdesc_bno, buffer);
    }

    status = fsw_dnode_create_root(vol, EXT4_ROOT_INO, &vol->g.root);
    if (status)
        return status;

    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext4_volume_mount: success, blocksize %d\n"), blocksize));

    return FSW_SUCCESS;
}

static void fsw_ext4_volume_free(struct fsw_ext4_volume *vol)
{
    if (vol->sb)
        fsw_free(vol->sb);
    if (vol->inotab_bno)
        fsw_free(vol->inotab_bno);
}

static fsw_status_t fsw_ext4_volume_stat(struct fsw_ext4_volume *vol, struct fsw_volume_stat *sb)
{
    fsw_u64         count;

    count = vol->sb->s_blocks_count_lo;
    if (vol->sb->s_desc_size >= EXT4_MIN_DESC_SIZE_64BIT)
        count |= (fsw_u64)vol->sb->s_blocks_count_hi << 32;
    sb->total_bytes = count * vol->g.log_blocksize;

    count = vol->sb->s_free_blocks_count_lo;
    if (vol->sb->s_desc_size >= EXT4_MIN_DESC_SIZE_64BIT)
        count |= (fsw_u64)vol->sb->s_free_blocks_count_hi << 32;
    sb->free_bytes  = count * vol->g.log_blocksize;

    return FSW_SUCCESS;
}

static fsw_status_t fsw_ext4_dnode_fill(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno)
{
    fsw_status_t    status;
    fsw_u32         groupno, ino_in_group, ino_index;
    fsw_u64         ino_bno;
    fsw_u8          *buffer;

    if (dno->raw)
        return FSW_SUCCESS;

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
    /*
     * Only the low size word reaches the shared dnode. A 64-bit inode-table
     * address above identifies where to fetch the inode and does not extend
     * the file-length value exported here.
     */
    dno->g.size = dno->raw->i_size_lo;  

    if (S_ISREG(dno->raw->i_mode))
        dno->g.type = FSW_DNODE_TYPE_FILE;
    else if (S_ISDIR(dno->raw->i_mode))
        dno->g.type = FSW_DNODE_TYPE_DIR;
    else if (S_ISLNK(dno->raw->i_mode))
        dno->g.type = FSW_DNODE_TYPE_SYMLINK;
    else
        dno->g.type = FSW_DNODE_TYPE_SPECIAL;

    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext4_dnode_fill: inode flags %x\n"), dno->raw->i_flags));
    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext4_dnode_fill: i_mode %x\n"), dno->raw->i_mode));
    return FSW_SUCCESS;
}

static void fsw_ext4_dnode_free(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno)
{
    if (dno->raw)
        fsw_free(dno->raw);
}

static fsw_status_t fsw_ext4_dnode_stat(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                        struct fsw_dnode_stat *sb)
{
    sb->used_bytes = dno->raw->i_blocks_lo * EXT4_BLOCK_SIZE(vol->sb);    
    fsw_store_time_posix(sb, FSW_DNODE_STAT_CTIME, dno->raw->i_ctime);
    fsw_store_time_posix(sb, FSW_DNODE_STAT_ATIME, dno->raw->i_atime);
    fsw_store_time_posix(sb, FSW_DNODE_STAT_MTIME, dno->raw->i_mtime);
    fsw_store_attr_posix(sb, dno->raw->i_mode);

    return FSW_SUCCESS;
}

/**
 * Choose the mapping per inode. EXT4_INODE_EXTENTS selects the tree rooted inside
 * i_block. Older inodes reinterpret those same bytes as direct and indirect block
 * pointers. The initial result spans one logical block unless the selected mapper
 * can extend it.
 */
static fsw_status_t fsw_ext4_get_extent(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                        struct fsw_extent *extent)
{

    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext4_get_extent: inode %d, block %d\n"), dno->g.dnode_id, extent->log_start));
    extent->type = FSW_EXTENT_TYPE_PHYSBLOCK;
    extent->log_count = 1;

    if(dno->raw->i_flags & 1 << EXT4_INODE_EXTENTS)
    {
       FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext4_get_extent: inode %d uses extents\n"), dno->g.dnode_id));
       return fsw_ext4_get_by_extent(vol, dno, extent);
    }
    else
    {
       FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext4_get_extent: inode %d uses direct/indirect block addressing\n"),
           dno->g.dnode_id));
       return fsw_ext4_get_by_blkaddr(vol, dno, extent);
    }
}

/**
 * The inode contains the root header. At a matching leaf, subtract ee_block from
 * the requested logical block. Add this displacement to the 48-bit physical start
 * and subtract it from ee_len to report the remaining run. For a leaf mapping
 * logical 100 to physical 900 for 8 blocks, a request for 103 yields physical 903
 * and count 5.
 *
 * This implementation descends at the first index whose lower bound qualifies and
 * uses ee_len as stored. An exhausted inner scan has no explicit not-found exit
 * from the enclosing loop. These details matter for multi-index nodes, gaps and
 * unwritten extents.
 */
static fsw_status_t fsw_ext4_get_by_extent(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                        struct fsw_extent *extent)
{
    fsw_status_t  status;
    fsw_u32       bno, buf_offset;
    int           ext_cnt;
    void          *buffer;

    struct ext4_extent_header  *ext4_extent_header;
    struct ext4_extent_idx     *ext4_extent_idx;
    struct ext4_extent         *ext4_extent;

    bno = extent->log_start;

    buffer = (void *)dno->raw->i_block;
    buf_offset = 0;
    while(1) {
        ext4_extent_header = (struct ext4_extent_header *)((char *)buffer + buf_offset);
        buf_offset += sizeof(struct ext4_extent_header);
        FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext4_get_by_extent: extent header with %d entries\n"),
                      ext4_extent_header->eh_entries));
        if(ext4_extent_header->eh_magic != EXT4_EXT_MAGIC)
            return FSW_VOLUME_CORRUPTED;

        for(ext_cnt = 0;ext_cnt < ext4_extent_header->eh_entries;ext_cnt++)
        {
            if(ext4_extent_header->eh_depth == 0)
            {

                ext4_extent = (struct ext4_extent *)((char *)buffer + buf_offset);
                buf_offset += sizeof(struct ext4_extent);
                FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext4_get_by_extent: extent node cover %d...\n"), ext4_extent->ee_block));

                if(bno >= ext4_extent->ee_block && bno < ext4_extent->ee_block + ext4_extent->ee_len)
                {
                    /*
                     * FSW asks for a run starting at the requested logical
                     * block. Advance the combined physical address by the
                     * displacement within the leaf and shorten the returned
                     * run by exactly the same displacement.
                     */
                    extent->phys_start = ((fsw_u64)ext4_extent->ee_start_hi << 32) | ext4_extent->ee_start_lo;
                    extent->phys_start += (bno - ext4_extent->ee_block);
                    extent->log_count = ext4_extent->ee_len - (bno - ext4_extent->ee_block);
                    return FSW_SUCCESS;
                }
            }
            else
            {
                FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext4_get_by_extent: index extents, depth %d\n"),
                          ext4_extent_header->eh_depth));
                ext4_extent_idx = (struct ext4_extent_idx *)((char *)buffer + buf_offset);
                buf_offset += sizeof(struct ext4_extent_idx);

                FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext4_get_by_extent: index node covers block %d...\n"),
                          ext4_extent_idx->ei_block));
                if(bno >= ext4_extent_idx->ei_block)
                {

                    fsw_u64 phys_bno = ((fsw_u64)ext4_extent_idx->ei_leaf_hi << 32) | ext4_extent_idx->ei_leaf_lo;
                    status = fsw_block_get(vol, phys_bno, 1, (void **)&buffer);
                    if (status)
                        return status;
                    buf_offset = 0;
                    break;
                }
            }
        }
    }

    return FSW_NOT_FOUND;
}

/*
 * Resolves direct and multi-level indirect block pointers. A computed block
 * address must remain meaningful under the mounted volume's block geometry.
 */
static fsw_status_t fsw_ext4_get_by_blkaddr(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                        struct fsw_extent *extent)
{
    fsw_status_t    status;
    fsw_u32         bno, release_bno, buf_bcnt, file_bcnt;
    int             path[5], i;
    fsw_u32         *buffer;
    bno = extent->log_start;

    if (bno < EXT4_NDIR_BLOCKS) {
        path[0] = bno;
        path[1] = -1;
    } else {
        bno -= EXT4_NDIR_BLOCKS;

        if (bno < vol->ind_bcnt) {
            path[0] = EXT4_IND_BLOCK;
            path[1] = bno;
            path[2] = -1;
        } else {
            bno -= vol->ind_bcnt;

            if (bno < vol->dind_bcnt) {
                path[0] = EXT4_DIND_BLOCK;
                path[1] = bno / vol->ind_bcnt;
                path[2] = bno % vol->ind_bcnt;
                path[3] = -1;
            } else {
                bno -= vol->dind_bcnt;

                path[0] = EXT4_TIND_BLOCK;
                path[1] = bno / vol->dind_bcnt;
                path[2] = (bno / vol->ind_bcnt) % vol->ind_bcnt;
                path[3] = bno % vol->ind_bcnt;
                path[4] = -1;
            }
        }
    }

    buffer = dno->raw->i_block;
    buf_bcnt = EXT4_NDIR_BLOCKS;
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

static fsw_status_t fsw_ext4_dir_lookup(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                        struct fsw_string *lookup_name, struct fsw_ext4_dnode **child_dno_out)
{
    fsw_status_t    status;
    struct fsw_shandle shand;
    fsw_u32         child_ino;
    struct ext4_dir_entry entry;
    struct fsw_string entry_name;

    entry_name.type = FSW_STRING_TYPE_ISO88591;

    status = fsw_shandle_open(dno, &shand);
    if (status)
        return status;

    child_ino = 0;
    while (child_ino == 0) {

        status = fsw_ext4_read_dentry(&shand, &entry);
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

static fsw_status_t fsw_ext4_dir_read(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                      struct fsw_shandle *shand, struct fsw_ext4_dnode **child_dno_out)
{
    fsw_status_t    status;
    struct ext4_dir_entry entry;
    struct fsw_string entry_name;

    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_ext4_dir_read: started reading dir\n")));

    while (1) {

        status = fsw_ext4_read_dentry(shand, &entry);
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

/**
 * Read the eight-byte fixed prefix before interpreting the variable-length name.
 * An unused inode skips the rest of its record. For live entries the declared
 * name must fit in rec_len. Advance past alignment padding after copying the name
 * so the next read starts at a record boundary.
 */
static fsw_status_t fsw_ext4_read_dentry(struct fsw_shandle *shand, struct ext4_dir_entry *entry)
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

/**
 * Subtract external attribute blocks when deciding whether the link text fits in
 * the inode itself. The size check against sizeof(i_block) precedes both
 * branches. Even the separate data-read branch is therefore reached only for
 * targets within that bound.
 */
static fsw_status_t fsw_ext4_readlink(struct fsw_ext4_volume *vol, struct fsw_ext4_dnode *dno,
                                      struct fsw_string *link_target)
{
    fsw_status_t    status;
    int             ea_blocks;
    struct fsw_string s;

    if ((dno->g.size > FSW_PATH_MAX) || (dno->g.size > sizeof(dno->raw->i_block)))
        return FSW_VOLUME_CORRUPTED;

    ea_blocks = dno->raw->i_file_acl_lo ? (vol->g.log_blocksize >> 9) : 0;

    if (dno->raw->i_blocks_lo - ea_blocks == 0) {

        s.type = FSW_STRING_TYPE_ISO88591;
        s.size = s.len = (int)dno->g.size;
        s.data = dno->raw->i_block;
        status = fsw_strdup_coerce(link_target, vol->g.host_string_type, &s);
    } else {

        status = fsw_dnode_readlink_data(dno, link_target);
    }

    return status;
}
