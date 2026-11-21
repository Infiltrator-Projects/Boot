/*
 * Filesystem-neutral block cache, dnode tree and file-handle read engine.
 * Backend fstype tables supply mount, extent and directory operations while
 * the host table supplies block I/O and allocation. Cached block references
 * and dnode refcounts are explicit lifetime contracts across both layers.
 */

#include "fsw_core.h"
#include "fsw_efi.h"

static void fsw_blockcache_free(struct fsw_volume *vol);

#define MAX_CACHE_LEVEL (5)

/*
 * Allocate the volume using the selected backend's structure size. Attach the
 * host callbacks and provisional 512-byte geometry before invoking
 * volume_mount. Failure goes through fsw_unmount, so the backend's cleanup
 * must tolerate partially initialised state.
 */
fsw_status_t fsw_mount(void *host_data,
                       struct fsw_host_table *host_table,
                       struct fsw_fstype_table *fstype_table,
                       struct fsw_volume **vol_out)
{
    fsw_status_t    status;
    struct fsw_volume *vol;

    status = fsw_alloc_zero(fstype_table->volume_struct_size, (void **)&vol);
    if (status)
        return status;

    vol->phys_blocksize = 512;
    vol->log_blocksize  = 512;
    vol->label.type     = FSW_STRING_TYPE_EMPTY;
    vol->host_data      = host_data;
    vol->host_table     = host_table;
    vol->fstype_table   = fstype_table;
    vol->host_string_type = host_table->native_string_type;

    status = vol->fstype_table->volume_mount(vol);
    if (status)
        goto errorexit;

    *vol_out = vol;
    return FSW_SUCCESS;

errorexit:
    fsw_unmount(vol);
    return status;
}

void fsw_unmount(struct fsw_volume *vol)
{
    if (vol->root)
        fsw_dnode_release(vol->root);

    vol->fstype_table->volume_free(vol);

    fsw_blockcache_free(vol);
    fsw_strfree(&vol->label);
    fsw_free(vol);
}

fsw_status_t fsw_volume_stat(struct fsw_volume *vol, struct fsw_volume_stat *sb)
{
    return vol->fstype_table->volume_stat(vol, sb);
}

/*
 * A block number has meaning only with the geometry that produced it. Mount
 * code can switch from probe geometry to volume geometry. Drop old cache
 * entries and pass both geometries to the host before installing the new
 * sizes.
 */
void fsw_set_blocksize(struct fsw_volume *vol, fsw_u32 phys_blocksize, fsw_u32 log_blocksize)
{

    fsw_blockcache_free(vol);

    vol->host_table->change_blocksize(vol,
                                      vol->phys_blocksize, vol->log_blocksize,
                                      phys_blocksize, log_blocksize);

    vol->phys_blocksize = phys_blocksize;
    vol->log_blocksize = log_blocksize;
}

/*
 * A hit pins the existing buffer and raises its retention level when
 * requested. On a miss, prefer unused slots, then unpinned entries starting
 * at the lowest retention level. Mark the chosen slot invalid until the host
 * read succeeds. Each successful acquisition needs a matching release.
 */
fsw_status_t fsw_block_get(struct VOLSTRUCTNAME *vol, fsw_u64 phys_bno, fsw_u32 cache_level, void **buffer_out)
{
    fsw_status_t    status;
    fsw_u32         i, discard_level, new_bcache_size;
    struct fsw_blockcache *new_bcache;

    if (cache_level > MAX_CACHE_LEVEL)
        cache_level = MAX_CACHE_LEVEL;

    for (i = 0; i < vol->bcache_size; i++) {
        if (vol->bcache[i].phys_bno == phys_bno) {

            if (vol->bcache[i].cache_level < cache_level)
                vol->bcache[i].cache_level = cache_level;   
            vol->bcache[i].refcount++;
            *buffer_out = vol->bcache[i].data;
            return FSW_SUCCESS;
        }
    }

    for (i = 0; i < vol->bcache_size; i++) {
        if (vol->bcache[i].phys_bno == (fsw_u64)FSW_INVALID_BNO)
            break;
    }
    if (i >= vol->bcache_size) {
        for (discard_level = 0; discard_level <= MAX_CACHE_LEVEL; discard_level++) {
            for (i = 0; i < vol->bcache_size; i++) {
                if (vol->bcache[i].refcount == 0 && vol->bcache[i].cache_level <= discard_level)
                    break;
            }
            if (i < vol->bcache_size)
                break;
        }
    }
    if (i >= vol->bcache_size) {

        if (vol->bcache_size < 16)
            new_bcache_size = 16;
        else
            new_bcache_size = vol->bcache_size << 1;
        status = fsw_alloc(new_bcache_size * sizeof(struct fsw_blockcache), &new_bcache);
        if (status)
            return status;
        if (vol->bcache_size > 0)
            fsw_memcpy(new_bcache, vol->bcache, vol->bcache_size * sizeof(struct fsw_blockcache));
        for (i = vol->bcache_size; i < new_bcache_size; i++) {
            new_bcache[i].refcount = 0;
            new_bcache[i].cache_level = 0;
            new_bcache[i].phys_bno = (fsw_u64)FSW_INVALID_BNO;
            new_bcache[i].data = NULL;
        }
        i = vol->bcache_size;

        if (vol->bcache != NULL)
            fsw_free(vol->bcache);
        vol->bcache = new_bcache;
        vol->bcache_size = new_bcache_size;
    }
    vol->bcache[i].phys_bno = (fsw_u64)FSW_INVALID_BNO;

    if (vol->bcache[i].data == NULL) {
        status = fsw_alloc(vol->phys_blocksize, &vol->bcache[i].data);
        if (status)
            return status;
    }
    status = vol->host_table->read_block(vol, phys_bno, vol->bcache[i].data);
    if (status)
        return status;

    vol->bcache[i].phys_bno = phys_bno;
    vol->bcache[i].cache_level = cache_level;
    vol->bcache[i].refcount = 1;
    *buffer_out = vol->bcache[i].data;
    return FSW_SUCCESS;
}

/*
 * Decrements a matching cache entry reference; call only for a successfully
 * obtained block and do not retain its data after it becomes evictable.
 */
void fsw_block_release(struct VOLSTRUCTNAME *vol, fsw_u64 phys_bno, void *buffer)
{
    fsw_u32 i;

    for (i = 0; i < vol->bcache_size; i++) {
        if (vol->bcache[i].phys_bno == phys_bno && vol->bcache[i].refcount > 0)
            vol->bcache[i].refcount--;
    }
}

static void fsw_blockcache_free(struct fsw_volume *vol)
{
    fsw_u32 i;

    for (i = 0; i < vol->bcache_size; i++) {
        if (vol->bcache[i].data != NULL)
            fsw_free(vol->bcache[i].data);
    }
    if (vol->bcache != NULL) {
        fsw_free(vol->bcache);
        vol->bcache = NULL;
    }
    vol->bcache_size = 0;
    fsw_efi_clear_cache();
}

static void fsw_dnode_register(struct fsw_volume *vol, struct fsw_dnode *dno)
{
    dno->next = vol->dnode_head;
    if (vol->dnode_head != NULL)
        vol->dnode_head->prev = dno;
    dno->prev = NULL;
    vol->dnode_head = dno;
}

fsw_status_t fsw_dnode_create_root_with_tree(struct fsw_volume *vol, fsw_u64 tree_id, fsw_u64 dnode_id, struct fsw_dnode **dno_out)
{
    fsw_status_t    status;
    struct fsw_dnode *dno;

    status = fsw_alloc_zero(vol->fstype_table->dnode_struct_size, (void **)&dno);
    if (status)
        return status;

    dno->vol = vol;
    dno->parent = NULL;
    dno->tree_id = tree_id;
    dno->dnode_id = dnode_id;
    dno->type = FSW_DNODE_TYPE_DIR;
    dno->refcount = 1;
    dno->name.type = FSW_STRING_TYPE_EMPTY;

    fsw_dnode_register(vol, dno);

    *dno_out = dno;
    return FSW_SUCCESS;
}

fsw_status_t fsw_dnode_create_root(struct fsw_volume *vol, fsw_u64 dnode_id, struct fsw_dnode **dno_out)
{
	return fsw_dnode_create_root_with_tree( vol, 0, dnode_id, dno_out);
}

fsw_status_t fsw_dnode_create_with_tree(struct fsw_dnode *parent_dno, fsw_u64 tree_id, fsw_u64 dnode_id, int type,
                              struct fsw_string *name, struct fsw_dnode **dno_out)
{
    fsw_status_t    status;
    struct fsw_volume *vol = parent_dno->vol;
    struct fsw_dnode *dno;

    for (dno = vol->dnode_head; dno; dno = dno->next) {
        /*
         * Dnode identity is local to a tree as well as a volume. A lookup hit
         * retains the existing object. A subvolume can reuse an inode number,
         * so omitting tree_id would alias entries across roots.
         */
        if (dno->dnode_id == dnode_id && dno->tree_id == tree_id) {
            fsw_dnode_retain(dno);
            *dno_out = dno;
            return FSW_SUCCESS;
        }
    }

    status = fsw_alloc_zero(vol->fstype_table->dnode_struct_size, (void **)&dno);
    if (status)
        return status;

    dno->vol = vol;
    dno->parent = parent_dno;
    fsw_dnode_retain(dno->parent);
    dno->tree_id = tree_id;
    dno->dnode_id = dnode_id;
    dno->type = type;
    dno->refcount = 1;
    status = fsw_strdup_coerce(&dno->name, vol->host_table->native_string_type, name);
    if (status) {
        fsw_free(dno);
        return status;
    }

    fsw_dnode_register(vol, dno);

    *dno_out = dno;
    return FSW_SUCCESS;
}

fsw_status_t fsw_dnode_create(struct fsw_dnode *parent_dno, fsw_u64 dnode_id, int type,
                              struct fsw_string *name, struct fsw_dnode **dno_out)
{
	return fsw_dnode_create_with_tree(parent_dno, 0, dnode_id, type, name, dno_out);
}

void fsw_dnode_retain(struct fsw_dnode *dno)
{
    dno->refcount++;
}

void fsw_dnode_release(struct fsw_dnode *dno)
{
    struct fsw_volume *vol = dno->vol;
    struct fsw_dnode *parent_dno;

    dno->refcount--;

    if (dno->refcount == 0) {
        parent_dno = dno->parent;

        if (dno->next)
            dno->next->prev = dno->prev;
        if (dno->prev)
            dno->prev->next = dno->next;
        if (vol->dnode_head == dno)
            vol->dnode_head = dno->next;

        vol->fstype_table->dnode_free(vol, dno);

        fsw_strfree(&dno->name);
        fsw_free(dno);

        if (parent_dno)
            fsw_dnode_release(parent_dno);
    }
}

fsw_status_t fsw_dnode_fill(struct fsw_dnode *dno)
{

    return dno->vol->fstype_table->dnode_fill(dno->vol, dno);
}

fsw_status_t fsw_dnode_stat(struct fsw_dnode *dno, struct fsw_dnode_stat *sb)
{
    fsw_status_t    status;

    status = fsw_dnode_fill(dno);
    if (status)
        return status;

    sb->used_bytes = 0;
    status = dno->vol->fstype_table->dnode_stat(dno->vol, dno, sb);
    if (!status && !sb->used_bytes)
        sb->used_bytes = FSW_U64_DIV(dno->size + dno->vol->log_blocksize - 1, dno->vol->log_blocksize);
    return status;
}

fsw_status_t fsw_dnode_lookup(struct fsw_dnode *dno,
                              struct fsw_string *lookup_name, struct fsw_dnode **child_dno_out)
{
    fsw_status_t    status;

    status = fsw_dnode_fill(dno);
    if (status)
        return status;
    if (dno->type != FSW_DNODE_TYPE_DIR)
        return FSW_UNSUPPORTED;

    return dno->vol->fstype_table->dir_lookup(dno->vol, dno, lookup_name, child_dno_out);
}

/*
 * Walk borrowed string slices while retaining the current dnode. An empty
 * first component selects the root, while an interior empty component retains
 * the current node. Resolve intermediate symlinks before asking the backend
 * to look up a child.
 */
fsw_status_t fsw_dnode_lookup_path(struct fsw_dnode *dno,
                                   struct fsw_string *lookup_path, char separator,
                                   struct fsw_dnode **child_dno_out)
{
    fsw_status_t    status;
    struct fsw_volume *vol = dno->vol;
    struct fsw_dnode *child_dno = NULL;
    struct fsw_string lookup_name;
    struct fsw_string remaining_path;
    int             root_if_empty;

    remaining_path = *lookup_path;
    fsw_dnode_retain(dno);

    for (root_if_empty = 1; fsw_strlen(&remaining_path) > 0; root_if_empty = 0) {

        fsw_strsplit(&lookup_name, &remaining_path, separator);

        FSW_MSG_DEBUG((FSW_MSGSTR("fsw_dnode_lookup_path: split into %d '%s' and %d '%s'\n"),
                       lookup_name.len, lookup_name.data,
                       remaining_path.len, remaining_path.data));

        if (fsw_strlen(&lookup_name) == 0) {         
            if (root_if_empty)
                child_dno = vol->root;
            else
                child_dno = dno;
            fsw_dnode_retain(child_dno);

        } else {

            status = fsw_dnode_fill(dno);
            if (status)
                goto errorexit;

            if (dno->type == FSW_DNODE_TYPE_SYMLINK) {
                status = fsw_dnode_resolve(dno, &child_dno);
                if (status)
                    goto errorexit;

                fsw_dnode_release(dno);
                dno = child_dno;    
                child_dno = NULL;

                status = fsw_dnode_fill(dno);
                if (status)
                    goto errorexit;
            }

            if (dno->type != FSW_DNODE_TYPE_DIR) {
                return FSW_UNSUPPORTED;
                goto errorexit;
            }

            if (fsw_streq_cstr(&lookup_name, ".")) {     
                child_dno = dno;
                fsw_dnode_retain(child_dno);

            } else if (fsw_streq_cstr(&lookup_name, "..")) {    
                if (dno->parent == NULL) {

                    status = FSW_NOT_FOUND;
                    goto errorexit;
                }
                child_dno = dno->parent;
                fsw_dnode_retain(child_dno);

            } else {

                status = vol->fstype_table->dir_lookup(vol, dno, &lookup_name, &child_dno);
                if (status)
                    goto errorexit;
            }
        }

        fsw_dnode_release(dno);
        dno = child_dno;    
        child_dno = NULL;

        FSW_MSG_DEBUG((FSW_MSGSTR("fsw_dnode_lookup_path: now at inode %d\n"), dno->dnode_id));
    }

    *child_dno_out = dno;
    return FSW_SUCCESS;

errorexit:
    FSW_MSG_DEBUG((FSW_MSGSTR("fsw_dnode_lookup_path: leaving with error %d\n"), status));
    fsw_dnode_release(dno);
    if (child_dno != NULL)
        fsw_dnode_release(child_dno);
    return status;
}

fsw_status_t fsw_dnode_dir_read(struct fsw_shandle *shand, struct fsw_dnode **child_dno_out)
{
    fsw_status_t    status;
    struct fsw_dnode *dno = shand->dnode;
    fsw_u64         saved_pos;

    if (dno->type != FSW_DNODE_TYPE_DIR)
        return FSW_UNSUPPORTED;
    /*
     * Directory cursors belong to the backend. Save the opaque value before
     * dispatch so a failed backend read can be retried from the same entry.
     */
    saved_pos = shand->pos;
    status = dno->vol->fstype_table->dir_read(dno->vol, dno, shand, child_dno_out);
    if (status)
        shand->pos = saved_pos;
    return status;
}

fsw_status_t fsw_dnode_readlink(struct fsw_dnode *dno, struct fsw_string *target_name)
{
    fsw_status_t    status;

    status = fsw_dnode_fill(dno);
    if (status)
        return status;
    if (dno->type != FSW_DNODE_TYPE_SYMLINK)
        return FSW_UNSUPPORTED;

    return dno->vol->fstype_table->readlink(dno->vol, dno, target_name);
}

fsw_status_t fsw_dnode_readlink_data(struct fsw_dnode *dno, struct fsw_string *link_target)
{
    fsw_status_t    status;
    struct fsw_shandle shand;
    fsw_u32         buffer_size;
    char            buffer[FSW_PATH_MAX];

    struct fsw_string s;

    if (dno->size > FSW_PATH_MAX)
        return FSW_VOLUME_CORRUPTED;

    s.type = FSW_STRING_TYPE_ISO88591;
    s.size = s.len = (int)dno->size;
    s.data = buffer;

    status = fsw_shandle_open(dno, &shand);
    if (status)
        return status;
    buffer_size = (fsw_u32)s.size;
    status = fsw_shandle_read(&shand, &buffer_size, buffer);
    fsw_shandle_close(&shand);
    if (status)
        return status;
    if ((int)buffer_size < s.size)
        return FSW_VOLUME_CORRUPTED;

    status = fsw_strdup_coerce(link_target, dno->vol->host_string_type, &s);
    return status;
}

fsw_status_t fsw_dnode_resolve(struct fsw_dnode *dno, struct fsw_dnode **target_dno_out)
{
    fsw_status_t    status;
    struct fsw_string target_name;
    struct fsw_dnode *target_dno;

    int link_count = 40;

    fsw_dnode_retain(dno);

    while (--link_count > 0) {

        status = fsw_dnode_fill(dno);
        if (status)
            goto errorexit;
        if (dno->type != FSW_DNODE_TYPE_SYMLINK) {

            *target_dno_out = dno;
            return FSW_SUCCESS;
        }
        if (dno->parent == NULL) {     
            status = FSW_NOT_FOUND;
            goto errorexit;
        }

        status = fsw_dnode_readlink(dno, &target_name);
        if (status)
            goto errorexit;

        status = fsw_dnode_lookup_path(dno->parent, &target_name, '/', &target_dno);
        fsw_strfree(&target_name);
        if (status)
            goto errorexit;

        fsw_dnode_release(dno);
        dno = target_dno;    
    }
    if(link_count == 0)
      status = FSW_NOT_FOUND;

errorexit:
    fsw_dnode_release(dno);
    return status;
}

fsw_status_t fsw_shandle_open(struct fsw_dnode *dno, struct fsw_shandle *shand)
{
    fsw_status_t    status;
    struct fsw_volume *vol = dno->vol;

    status = vol->fstype_table->dnode_fill(vol, dno);
    if (status)
        return status;

    fsw_dnode_retain(dno);

    shand->dnode = dno;
    shand->pos = 0;
    shand->extent.type = FSW_EXTENT_TYPE_INVALID;

    return FSW_SUCCESS;
}

void fsw_shandle_close(struct fsw_shandle *shand)
{
    if (shand->extent.type == FSW_EXTENT_TYPE_BUFFER)
        fsw_free(shand->extent.buffer);
    fsw_dnode_release(shand->dnode);
}

/*
 * Maps the file position through a backend extent into physical, buffer or
 * sparse-zero data; partial reads update position and returned byte count.
 * AUDIT: the local position is narrowed to fsw_u32; review files > 4 GiB.
 */
fsw_status_t fsw_shandle_read(struct fsw_shandle *shand, fsw_u32 *buffer_size_inout, void *buffer_in)
{
    fsw_status_t    status;
    struct fsw_dnode *dno = shand->dnode;
    struct fsw_volume *vol = dno->vol;
    fsw_u8          *buffer, *block_buffer;
    fsw_u64         buflen, copylen, pos;
    fsw_u64         log_bno, pos_in_extent, phys_bno, pos_in_physblock;
    fsw_u32         cache_level;

    if (shand->pos >= dno->size) {    
        *buffer_size_inout = 0;
        return FSW_SUCCESS;
    }

    buffer = buffer_in;
    buflen = *buffer_size_inout;
    pos = (fsw_u32)shand->pos;
    cache_level = (dno->type != FSW_DNODE_TYPE_FILE) ? 1 : 0;

    if (buflen > dno->size - pos)
        buflen = (fsw_u32)(dno->size - pos);

    while (buflen > 0) {

        log_bno = FSW_U64_DIV(pos, vol->log_blocksize);
        if (shand->extent.type == FSW_EXTENT_TYPE_INVALID ||
            log_bno < shand->extent.log_start ||
            log_bno >= shand->extent.log_start + shand->extent.log_count) {

            if (shand->extent.type == FSW_EXTENT_TYPE_BUFFER)
                fsw_free(shand->extent.buffer);

            shand->extent.log_start = log_bno;
            status = vol->fstype_table->get_extent(vol, dno, &shand->extent);
            if (status) {
                shand->extent.type = FSW_EXTENT_TYPE_INVALID;
                return status;
            }
        }

        pos_in_extent = pos - shand->extent.log_start * vol->log_blocksize;

        if (shand->extent.type == FSW_EXTENT_TYPE_PHYSBLOCK) {

            phys_bno = shand->extent.phys_start + FSW_U64_DIV(pos_in_extent, vol->phys_blocksize);
            pos_in_physblock = pos_in_extent & (vol->phys_blocksize - 1);
            copylen = vol->phys_blocksize - pos_in_physblock;
            if (copylen > buflen)
                copylen = buflen;

            status = fsw_block_get(vol, phys_bno, cache_level, (void **)&block_buffer);
            if (status)
                return status;

            fsw_memcpy(buffer, block_buffer + pos_in_physblock, copylen);
            fsw_block_release(vol, phys_bno, block_buffer);

        } else if (shand->extent.type == FSW_EXTENT_TYPE_BUFFER) {
            copylen = shand->extent.log_count * vol->log_blocksize - pos_in_extent;
            if (copylen > buflen)
                copylen = buflen;
            fsw_memcpy(buffer, (fsw_u8 *)shand->extent.buffer + pos_in_extent, copylen);

        } else {    
            copylen = shand->extent.log_count * vol->log_blocksize - pos_in_extent;
            if (copylen > buflen)
                copylen = buflen;
            fsw_memzero(buffer, copylen);

        }

        buffer += copylen;
        buflen -= copylen;
        pos    += copylen;
    }

    /*
     * fsw_shandle_read commits the byte count and cursor here. Earlier
     * failures may follow copies into the caller's buffer, but return before
     * this commit. Callers cannot infer a successful partial read from those
     * bytes.
     */
    *buffer_size_inout = (fsw_u32)(pos - shand->pos);
    shand->pos = pos;

    return FSW_SUCCESS;
}
