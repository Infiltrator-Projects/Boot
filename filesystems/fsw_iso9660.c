/*
 * ISO 9660 read-only optical filesystem implementation. Volume descriptors,
 * logical blocks and directory records come from media, not from an EFI file
 * namespace. Directory record lengths and filename encodings are explicit
 * validation boundaries; device block size may differ from file block size.
 * Rock Ridge and SUSP continuation records can redirect parsing to additional
 * sectors; an apparently valid directory-record length alone does not
 * establish the bounds or termination of the continuation chain.
 */

#include "fsw_iso9660.h"

#ifndef DEBUG_ISO
#define DEBUG_ISO 1
#endif

#if DEBUG_ISO == 2
#define DBG(x...)	AsciiPrint(x)
#elif DEBUG_ISO == 1
#define DBG(x...)	BootLog(x)
#else
#define DBG(x...)
#endif

static fsw_status_t fsw_iso9660_volume_mount(struct fsw_iso9660_volume *vol);
static void         fsw_iso9660_volume_free(struct fsw_iso9660_volume *vol);
static fsw_status_t fsw_iso9660_volume_stat(struct fsw_iso9660_volume *vol, struct fsw_volume_stat *sb);

static fsw_status_t fsw_iso9660_dnode_fill(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno);
static void         fsw_iso9660_dnode_free(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno);
static fsw_status_t fsw_iso9660_dnode_stat(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno,
                                           struct fsw_dnode_stat *sb);
static fsw_status_t fsw_iso9660_get_extent(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno,
                                           struct fsw_extent *extent);

static fsw_status_t fsw_iso9660_dir_lookup(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno,
                                           struct fsw_string *lookup_name, struct fsw_iso9660_dnode **child_dno);
static fsw_status_t fsw_iso9660_dir_read(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno,
                                         struct fsw_shandle *shand, struct fsw_iso9660_dnode **child_dno);
static fsw_status_t fsw_iso9660_read_dirrec(struct fsw_iso9660_volume *vol, struct fsw_shandle *shand, struct iso9660_dirrec_buffer *dirrec_buffer);

static fsw_status_t fsw_iso9660_readlink(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno,
                                         struct fsw_string *link);

/*
 * Locate the Rock Ridge SUSP SP marker inside one ISO directory record. Its
 * signature and 0xBE/0xEF check select the extension mechanism. The record
 * length comes from media and is bounded before inspecting a candidate
 * as a complete SP structure. Finding SP enables extension interpretation,
 * not general validation of continuation areas or arbitrary Rock Ridge data.
 */
/* Search the system-use region of a counted ISO directory record for the
 * SUSP 'SP' signature and 0xBE/0xEF check bytes. The result is a borrowed
 * pointer into dirrec's storage, not an allocated copy. This bytewise scan
 * does not prove the surrounding Rock Ridge fields are well-formed.
 */
static fsw_status_t rr_find_sp(struct iso9660_dirrec *dirrec, struct fsw_rock_ridge_susp_sp **psp);
static fsw_status_t rr_find_nm(struct fsw_iso9660_volume *vol, struct iso9660_dirrec *dirrec, int off, struct fsw_string *str);
static fsw_status_t rr_read_ce(struct fsw_iso9660_volume *vol, union fsw_rock_ridge_susp_ce *ce, fsw_u8 *begin);

struct fsw_fstype_table   FSW_FSTYPE_TABLE_NAME(iso9660) = {
    { FSW_STRING_TYPE_ISO88591, 4, 4, "iso9660" },
    sizeof(struct fsw_iso9660_volume),
    sizeof(struct fsw_iso9660_dnode),

    fsw_iso9660_volume_mount,
    fsw_iso9660_volume_free,
    fsw_iso9660_volume_stat,
    fsw_iso9660_dnode_fill,
    fsw_iso9660_dnode_free,
    fsw_iso9660_dnode_stat,
    fsw_iso9660_get_extent,
    fsw_iso9660_dir_lookup,
    fsw_iso9660_dir_read,
    fsw_iso9660_readlink,
};

static fsw_status_t rr_find_sp(struct iso9660_dirrec *dirrec, struct fsw_rock_ridge_susp_sp **psp)
{
    fsw_u8 *r;
    int off = 0;
    struct fsw_rock_ridge_susp_sp *sp;
    *psp = NULL;
    off = (int)sizeof(*dirrec) + dirrec->file_identifier_length;
    if (off > dirrec->dirrec_length ||
        (int)sizeof(*sp) > dirrec->dirrec_length - off)
        return FSW_NOT_FOUND;
    r = (fsw_u8 *)dirrec + off;
    while (off <= dirrec->dirrec_length - (int)sizeof(*sp))
    {
        if (*r == 'S')
        {
            sp = (struct fsw_rock_ridge_susp_sp *)r;
            if(    sp->e.sig[0] == 'S'
                && sp->e.sig[1] == 'P'
                && sp->magic[0] == 0xbe
                && sp->magic[1] == 0xef)
            {
                *psp = sp;
                return FSW_SUCCESS;
            }
        }
        r++;
        off = (int)(r - (fsw_u8 *)dirrec);
    }
    *psp = NULL;
    return FSW_NOT_FOUND;
}

/*
 * Accumulate Rock Ridge NM name fragments into an owned fsw_string,
 * including continuation (CE) areas. The continuation offset and length
 * are on-disk inputs, and this routine does not uniformly validate every
 * variable-length entry before accessing it: malformed-image coverage is
 * still required. A returned name must be released by the consumer.
 */
static fsw_status_t rr_find_nm(struct fsw_iso9660_volume *vol, struct iso9660_dirrec *dirrec, int off, struct fsw_string *str)
{
    fsw_u8 *r, *begin;
    int fCe = 0;
    struct fsw_rock_ridge_susp_nm *nm;
    int limit = dirrec->dirrec_length;
    begin = (fsw_u8 *)dirrec;
    r = (fsw_u8 *)dirrec + off;
    str->data = NULL;
    str->len = 0;
    str->size = 0;
    str->type = 0;
    while(off < limit)
    {
        /*
         * A CE entry redirects parsing to a continuation area. Its offset and
         * length describe the auxiliary data. An NM name can span entries, so
         * the accumulated name must survive that change of input buffer.
         */
        if (r[0] == 'C' && r[1] == 'E' && r[2] == 28)
        {
            int rc;
            int ce_off;
            union fsw_rock_ridge_susp_ce *ce;
            if (fCe == 0)
                fsw_alloc_zero(ISO9660_BLOCKSIZE, (void *)&begin);
            fCe = 1;

            ce = (union fsw_rock_ridge_susp_ce *)r;
            limit = ISOINT(ce->X.len);
            ce_off = ISOINT(ce->X.offset);
            rc = rr_read_ce(vol, ce, begin);
            if (rc != FSW_SUCCESS)
            {
                fsw_free(begin);
                return rc;
            }
            begin += ce_off;
            r = begin;
        }
        if (r[0] == 'N' && r[1] == 'M')
        {
            nm = (struct fsw_rock_ridge_susp_nm *)r;
            if(    nm->e.sig[0] == 'N'
                && nm->e.sig[1] == 'M')
            {
                int len = 0;
                fsw_u8 *tmp = NULL;
                if (nm->flags & RR_NM_CURR)
                {
                     fsw_memdup(str->data, ".", 1);
                     str->len = 1;
                     goto done;
                }
                if (nm->flags & RR_NM_PARE)
                {
                     fsw_memdup(str->data, "..", 2);
                     str->len = 2;
                     goto done;
                }
                len = nm->e.len - sizeof(struct fsw_rock_ridge_susp_nm) + 1;
                fsw_alloc_zero(str->len + len, (void **)&tmp);
                if (str->data != NULL)
                {
                    fsw_memcpy(tmp, str->data, str->len);
                    fsw_free(str->data);
                }

                fsw_memcpy(tmp + str->len, &nm->name[0], len);
                str->data = tmp;
                str->len += len;

                if ((nm->flags & RR_NM_CONT) == 0)
                    goto done;
            }
        }
        r++;
        off = (int)(r - (fsw_u8 *)begin);
    }
    if(fCe == 1)
        fsw_free(begin);
    return FSW_NOT_FOUND;
done:
    str->type = FSW_STRING_TYPE_ISO88591;
    str->size = str->len;
    if(fCe == 1)
        fsw_free(begin);
    return FSW_SUCCESS;
}

/*
 * Read the device block named by a Rock Ridge CE reference into the
 * caller-supplied destination. This helper returns the host block-read
 * status and does not independently validate the continuation offset
 * or the advertised CE length.
 */
static fsw_status_t rr_read_ce(struct fsw_iso9660_volume *vol, union fsw_rock_ridge_susp_ce *ce, fsw_u8 *begin)
{
    int rc;

    rc = vol->g.host_table->read_block(&vol->g, ISOINT(ce->X.block_loc), begin);
    if (rc != FSW_SUCCESS)
        return rc;

    return FSW_SUCCESS;
}

/*
 * Finds and interprets ISO 9660 volume descriptors before establishing the
 * root directory. An optical block device is not necessarily valid ISO media.
 */
static fsw_status_t fsw_iso9660_volume_mount(struct fsw_iso9660_volume *vol)
{
    fsw_status_t    status;
    void            *buffer;
    fsw_u32         blockno;
    struct iso9660_volume_descriptor *voldesc;
    struct iso9660_primary_volume_descriptor *pvoldesc;
    fsw_u32         voldesc_type;
    int             i;
    struct fsw_string s;
    struct iso9660_dirrec rootdir;
    int sua_pos;
    char *sig;
    struct fsw_rock_ridge_susp_entry *entry;

    fsw_set_blocksize(vol, ISO9660_BLOCKSIZE, ISO9660_BLOCKSIZE);
    blockno = ISO9660_SUPERBLOCK_BLOCKNO;

    do {

        status = fsw_block_get(vol, blockno, 0, &buffer);
        if (status)
            return status;

        voldesc = (struct iso9660_volume_descriptor *)buffer;
        voldesc_type = voldesc->volume_descriptor_type;
        if (fsw_memeq(voldesc->standard_identifier, "CD001", 5)) {

            if (voldesc_type == 1 && voldesc->volume_descriptor_version == 1) {

                if (vol->primary_voldesc) {
                    fsw_free(vol->primary_voldesc);
                    vol->primary_voldesc = NULL;
                }
                status = fsw_memdup((void **)&vol->primary_voldesc, voldesc, ISO9660_BLOCKSIZE);
            }
        } else if (!fsw_memeq(voldesc->standard_identifier, "CD", 2)) {

            voldesc_type = 255;
        }

        fsw_block_release(vol, blockno, buffer);
        blockno++;
    } while (!status && voldesc_type != 255);
    if (status)
        return status;

    if (vol->primary_voldesc == NULL)
        return FSW_UNSUPPORTED;
    pvoldesc = vol->primary_voldesc;

    for (i = 32; i > 0; i--)
        if (pvoldesc->volume_identifier[i-1] != ' ')
            break;
    s.type = FSW_STRING_TYPE_ISO88591;
    s.size = s.len = i;
    s.data = pvoldesc->volume_identifier;
    status = fsw_strdup_coerce(&vol->g.label, vol->g.host_string_type, &s);
    if (status)
        return status;

    status = fsw_dnode_create_root(vol, ISO9660_SUPERBLOCK_BLOCKNO << ISO9660_BLOCKSIZE_BITS, &vol->g.root);
    if (status)
        return status;
    fsw_memcpy(&vol->g.root->dirrec, &pvoldesc->root_directory, sizeof(struct iso9660_dirrec));

    if (   pvoldesc->escape[0] == 0x25
        && pvoldesc->escape[1] == 0x2f
        && (   pvoldesc->escape[2] == 0x40
            || pvoldesc->escape[2] == 0x43
            || pvoldesc->escape[2] == 0x45))
    {

        vol->fJoliet = 1;
    }

    rootdir = pvoldesc->root_directory;
    sua_pos = (sizeof(struct iso9660_dirrec)) + rootdir.file_identifier_length + (rootdir.file_identifier_length % 2) - 2;

#if 1
    status = fsw_block_get(vol, ISOINT(rootdir.extent_location), 0, &buffer);
    sig = (char *)buffer + sua_pos;
    entry = (struct fsw_rock_ridge_susp_entry *)sig;
    if (   entry->sig[0] == 'S'
        && entry->sig[1] == 'P')
    {
        struct fsw_rock_ridge_susp_sp *sp = (struct fsw_rock_ridge_susp_sp *)entry;
        if (sp->magic[0] == 0xbe && sp->magic[1] == 0xef)
        {
            vol->fRockRidge = 1;
        } else {

        }
    }
#endif

    fsw_free(vol->primary_voldesc);
    vol->primary_voldesc = NULL;

    return FSW_SUCCESS;
}

/*
 * Release the retained primary volume descriptor; the generic volume
 * teardown owns the label, caches, root and enclosing volume allocation.
 */
static void fsw_iso9660_volume_free(struct fsw_iso9660_volume *vol)
{
    if (vol->primary_voldesc)
        fsw_free(vol->primary_voldesc);
}

/*
 * No free-space accounting is implemented for this optical reader:
 * both output counters are zero, not an assertion that the medium has
 * zero capacity or no remaining writable blocks.
 */
static fsw_status_t fsw_iso9660_volume_stat(struct fsw_iso9660_volume *vol, struct fsw_volume_stat *sb)
{
    sb->total_bytes = 0;  
    sb->free_bytes  = 0;
    return FSW_SUCCESS;
}

/*
 * Derive the logical byte size and file/directory type from the ISO
 * directory record. A recognised file flag does not establish that its
 * extent lies on readable media.
 */
static fsw_status_t fsw_iso9660_dnode_fill(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno)
{

    dno->g.size = ISOINT(dno->dirrec.data_length);
    if (dno->dirrec.file_flags & 0x02)
        dno->g.type = FSW_DNODE_TYPE_DIR;
    else
        dno->g.type = FSW_DNODE_TYPE_FILE;

    return FSW_SUCCESS;
}

/* ISO directory-record metadata is held directly inside the dnode and
 * requires no separate backend-private allocation. The generic FSW
 * destructor remains responsible for the name, parent and node storage.
 */
static void fsw_iso9660_dnode_free(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno)
{
}

/*
 * Round the logical file length up to an ISO 2048-byte allocation block
 * for FSW's used-byte statistic. This is a synthetic read-only accounting
 * value, not a physical write-allocation query.
 */
static fsw_status_t fsw_iso9660_dnode_stat(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno,
                                           struct fsw_dnode_stat *sb)
{
    sb->used_bytes = (dno->g.size + (ISO9660_BLOCKSIZE-1)) & ~(ISO9660_BLOCKSIZE-1);

    return FSW_SUCCESS;
}

/**
 * A directory record describes a contiguous run beginning at extent_location.
 * Return that whole mapping with log_start zero and round the data length up to
 * 2048-byte blocks. fsw_shandle_read separately clamps the final copy to the
 * file's logical byte length.
 */
static fsw_status_t fsw_iso9660_get_extent(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno,
                                           struct fsw_extent *extent)
{

    extent->type = FSW_EXTENT_TYPE_PHYSBLOCK;
    extent->phys_start = ISOINT(dno->dirrec.extent_location);
    extent->log_start = 0;
    extent->log_count = (ISOINT(dno->dirrec.data_length) + (ISO9660_BLOCKSIZE-1)) >> ISO9660_BLOCKSIZE_BITS;
    return FSW_SUCCESS;
}

/*
 * Resolves directory names from ISO naming conventions; matching rules
 * differ from UEFI FAT and ordinary host-filesystem path comparisons.
 */
static fsw_status_t fsw_iso9660_dir_lookup(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno,
                                           struct fsw_string *lookup_name, struct fsw_iso9660_dnode **child_dno_out)
{
    fsw_status_t    status;
    struct fsw_shandle shand;
    struct iso9660_dirrec_buffer dirrec_buffer;
    struct iso9660_dirrec *dirrec = &dirrec_buffer.dirrec;

    status = fsw_shandle_open(dno, &shand);
    if (status)
        return status;

    while (1) {

        status = fsw_iso9660_read_dirrec(vol, &shand, &dirrec_buffer);
        if (status)
            goto errorexit;
        if (dirrec->dirrec_length == 0) {

            status = FSW_NOT_FOUND;
            goto errorexit;
        }

        if (dirrec->file_identifier_length == 1 &&
            (dirrec->file_identifier[0] == 0 || dirrec->file_identifier[0] == 1))
            continue;

        if (fsw_streq(lookup_name, &dirrec_buffer.name))   
            break;
    }

    status = fsw_dnode_create(dno, dirrec_buffer.ino, FSW_DNODE_TYPE_UNKNOWN, &dirrec_buffer.name, child_dno_out);
    if (status == FSW_SUCCESS)
        fsw_memcpy(&(*child_dno_out)->dirrec, dirrec, sizeof(struct iso9660_dirrec));

errorexit:
    fsw_shandle_close(&shand);
    return status;
}

/**
 * Skip zero-length sector padding and the reserved single-byte identifiers for
 * the current and parent directory. Create the child dnode from the derived name
 * and save the fixed directory record for later extent mapping. FSW_NOT_FOUND
 * marks the end of the directory stream.
 */
static fsw_status_t fsw_iso9660_dir_read(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno,
                                         struct fsw_shandle *shand, struct fsw_iso9660_dnode **child_dno_out)
{
    fsw_status_t    status;
    struct iso9660_dirrec_buffer dirrec_buffer;
    struct iso9660_dirrec *dirrec = &dirrec_buffer.dirrec;

    while (1) {

        if (shand->pos >= dno->g.size)
            return FSW_NOT_FOUND;  
        status = fsw_iso9660_read_dirrec(vol, shand, &dirrec_buffer);
        if (status)
            return status;
        if (dirrec->dirrec_length == 0)
        {

            shand->pos =(shand->pos & ~(vol->g.log_blocksize - 1)) + vol->g.log_blocksize;
            continue;
        }

        if (dirrec->file_identifier_length == 1 &&
            (dirrec->file_identifier[0] == 0 || dirrec->file_identifier[0] == 1))
            continue;
        break;
    }

    status = fsw_dnode_create(dno, dirrec_buffer.ino, FSW_DNODE_TYPE_UNKNOWN, &dirrec_buffer.name, child_dno_out);
    if (status == FSW_SUCCESS)
        fsw_memcpy(&(*child_dno_out)->dirrec, dirrec, sizeof(struct iso9660_dirrec));

    return status;
}

/**
 * Use the directory's byte position on the image as the synthetic inode number.
 * Read the 33-byte prefix first, then the remaining record including its
 * identifier. A zero length is a signal to the directory iterator to move to the
 * following block.
 *
 * Prefer a successfully decoded Rock Ridge NM alternate name. Otherwise expose
 * the counted ISO identifier with its version suffix and a trailing dot removed.
 * This fallback string borrows storage in the caller's record buffer.
 */
static fsw_status_t fsw_iso9660_read_dirrec(struct fsw_iso9660_volume *vol, struct fsw_shandle *shand, struct iso9660_dirrec_buffer *dirrec_buffer)
{
    fsw_status_t    status;
    fsw_u32         i, buffer_size, remaining_size, name_len;
    struct fsw_rock_ridge_susp_sp *sp = NULL;
    struct iso9660_dirrec *dirrec = &dirrec_buffer->dirrec;
    int sp_off;
    int rc;
    /*
     * ISO directory records have no inode field. FSW synthesises an
     * identifier from the containing directory's extent address in bytes plus
     * the record's stream offset, captured before the fixed header is
     * consumed.
     */
    dirrec_buffer->ino = (ISOINT(((struct fsw_iso9660_dnode *)shand->dnode)->dirrec.extent_location)
                          << ISO9660_BLOCKSIZE_BITS)
        + (fsw_u32)shand->pos;

    buffer_size = 33;
    status = fsw_shandle_read(shand, &buffer_size, dirrec);
    if (status)
    {

        return status;
    }

    if (buffer_size < 33 || dirrec->dirrec_length == 0) {

        fsw_u8 *r;
        r = (fsw_u8 *)dirrec;

        for(i = 0; i < buffer_size; ++i)
        {
            DEBUG((DEBUG_INFO, "r[%d]:%c", i, r[i]));
        }
        dirrec->dirrec_length = 0;
        return FSW_SUCCESS;
    }
    if (dirrec->dirrec_length < 33 ||
        dirrec->dirrec_length < 33 + dirrec->file_identifier_length)
        return FSW_VOLUME_CORRUPTED;

    buffer_size = remaining_size = dirrec->dirrec_length - 33;
    status = fsw_shandle_read(shand, &buffer_size, dirrec->file_identifier);
    if (status)
        return status;
    if (buffer_size < remaining_size)
        return FSW_VOLUME_CORRUPTED;

     if (vol->fRockRidge)
     {
         sp_off = sizeof(*dirrec) + dirrec->file_identifier_length;
         rc = rr_find_sp(dirrec, &sp);
         if (   rc == FSW_SUCCESS
             && sp != NULL)
         {
            sp_off = (fsw_u8 *)&sp[1] - (fsw_u8*)dirrec + sp->skip;
         }
         rc = rr_find_nm(vol, dirrec, sp_off,  &dirrec_buffer->name);
         if (rc == FSW_SUCCESS)
            return FSW_SUCCESS;
    }

    /*
     * Reaching this point selects the base ISO identifier. Drop the version
     * suffix and a trailing period when computing the display length. The
     * name here borrows storage from dirrec_buffer.
     */
    name_len = dirrec->file_identifier_length;
    for (i = name_len - 1; i > 0; i--) {
        if (dirrec->file_identifier[i] == ';') {
            name_len = i;    
            break;
        }
    }
    if (name_len > 0 && dirrec->file_identifier[name_len-1] == '.')
        name_len--;    
    dirrec_buffer->name.type = FSW_STRING_TYPE_ISO88591;
    dirrec_buffer->name.len = dirrec_buffer->name.size = name_len;
    dirrec_buffer->name.data = dirrec->file_identifier;

    return FSW_SUCCESS;
}

/*
 * Use the generic bounded data-backed link reader for a node no
 * larger than FSW_PATH_MAX. Link-target decoding is separate from the
 * Rock Ridge name-extension parser and may still fail on bad metadata.
 */
static fsw_status_t fsw_iso9660_readlink(struct fsw_iso9660_volume *vol, struct fsw_iso9660_dnode *dno,
                                         struct fsw_string *link_target)
{
    fsw_status_t    status;

    if (dno->g.size > FSW_PATH_MAX)
        return FSW_VOLUME_CORRUPTED;

    status = fsw_dnode_readlink_data(dno, link_target);

    return status;
}
