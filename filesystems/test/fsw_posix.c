 




 































#include "fsw_posix.h"


#ifndef FSTYPE
 
#define FSTYPE ext2
#endif


 

fsw_status_t fsw_posix_open_dno(struct fsw_posix_volume *pvol, const char *path, int required_type,
                                struct fsw_shandle *shand);

void fsw_posix_change_blocksize(struct fsw_volume *vol,
                              fsw_u32 old_phys_blocksize, fsw_u32 old_log_blocksize,
                              fsw_u32 new_phys_blocksize, fsw_u32 new_log_blocksize);
fsw_status_t fsw_posix_read_block(struct fsw_volume *vol, fsw_u32 phys_bno, void *buffer);

 



struct fsw_host_table   fsw_posix_host_table = {
    FSW_STRING_TYPE_ISO88591,

    fsw_posix_change_blocksize,
    fsw_posix_read_block
};

extern struct fsw_fstype_table   FSW_FSTYPE_TABLE_NAME(FSTYPE);


 



struct fsw_posix_volume * fsw_posix_mount(const char *path, struct fsw_fstype_table *fstype_table)
{
    fsw_status_t        status;
    struct fsw_posix_volume *pvol;

     
    status = fsw_alloc_zero(sizeof(struct fsw_posix_volume), (void **)&pvol);
    if (status)
        return NULL;
    pvol->fd = -1;

     
    pvol->fd = open(path, O_RDONLY, 0);
    if (pvol->fd < 0) {
        fprintf(stderr, "fsw_posix_mount: %s: %s\n", path, strerror(errno));
        fsw_free(pvol);
        return NULL;
    }

     
    if (fstype_table == NULL)
        fstype_table = &FSW_FSTYPE_TABLE_NAME(FSTYPE);
    status = fsw_mount(pvol, &fsw_posix_host_table, fstype_table, &pvol->vol);
    if (status) {
        fprintf(stderr, "fsw_posix_mount: fsw_mount returned %d\n", status);
        fsw_free(pvol);
        return NULL;
    }

    return pvol;
}

 



int fsw_posix_unmount(struct fsw_posix_volume *pvol)
{
    if (pvol->vol != NULL)
        fsw_unmount(pvol->vol);
    fsw_free(pvol);
    return 0;
}

 



struct fsw_posix_file * fsw_posix_open(struct fsw_posix_volume *pvol, const char *path, int flags, mode_t mode)
{
    fsw_status_t        status;
    struct fsw_posix_file *file;

     

     
    status = fsw_alloc(sizeof(struct fsw_posix_file), &file);
    if (status)
        return NULL;
    file->pvol = pvol;

     
    status = fsw_posix_open_dno(pvol, path, FSW_DNODE_TYPE_FILE, &file->shand);
    if (status) {
        fprintf(stderr, "fsw_posix_open: open_dno returned %d\n", status);
        fsw_free(file);
        return NULL;
    }

    return file;
}

 



ssize_t fsw_posix_read(struct fsw_posix_file *file, void *buf, size_t nbytes)
{
    fsw_status_t        status;
    fsw_u32             buffer_size;

    buffer_size = nbytes;
    status = fsw_shandle_read(&file->shand, &buffer_size, buf);
    if (status)
        return -1;
    return buffer_size;
}

 



off_t fsw_posix_lseek(struct fsw_posix_file *file, off_t offset, int whence)
{
    fsw_u64             base_offset = 0;

     
    base_offset = 0;
    if (whence == SEEK_CUR)
        base_offset = file->shand.pos;
    else if (whence == SEEK_END)
        base_offset = file->shand.dnode->size;

     
    if (offset < 0 && -offset > base_offset)
        file->shand.pos = 0;
    else
        file->shand.pos = base_offset + offset;

    return file->shand.pos;
}

 



int fsw_posix_close(struct fsw_posix_file *file)
{
    fsw_shandle_close(&file->shand);
    fsw_free(file);
    return 0;
}

 



struct fsw_posix_dir * fsw_posix_opendir(struct fsw_posix_volume *pvol, const char *path)
{
    fsw_status_t        status;
    struct fsw_posix_dir *dir;

     
    status = fsw_alloc(sizeof(struct fsw_posix_dir), &dir);
    if (status)
        return NULL;
    dir->pvol = pvol;

     
    status = fsw_posix_open_dno(pvol, path, FSW_DNODE_TYPE_DIR, &dir->shand);
    if (status) {
        fprintf(stderr, "fsw_posix_opendir: open_dno returned %d\n", status);
        fsw_free(dir);
        return NULL;
    }

    return dir;
}

 



struct dirent * fsw_posix_readdir(struct fsw_posix_dir *dir)
{
    fsw_status_t        status;
    struct fsw_dnode    *dno;
    static struct dirent dent;

     
    status = fsw_dnode_dir_read(&dir->shand, &dno);
    if (status) {
        if (status != 4)
            fprintf(stderr, "fsw_posix_readdir: fsw_dnode_dir_read returned %d\n", status);
        return NULL;
    }
    status = fsw_dnode_fill(dno);
    if (status) {
        fprintf(stderr, "fsw_posix_readdir: fsw_dnode_fill returned %d\n", status);
        fsw_dnode_release(dno);
        return NULL;
    }

     
    dent.d_fileno = dno->dnode_id;
    dent.d_reclen = 8 + dno->name.size + 1;
    switch (dno->type) {
        case FSW_DNODE_TYPE_FILE:
            dent.d_type = DT_REG;
            break;
        case FSW_DNODE_TYPE_DIR:
            dent.d_type = DT_DIR;
            break;
        case FSW_DNODE_TYPE_SYMLINK:
            dent.d_type = DT_LNK;
            break;
        default:
            dent.d_type = DT_UNKNOWN;
            break;
    }
#if 0
    dent.d_namlen = dno->name.size;
#endif
    memcpy(dent.d_name, dno->name.data, dno->name.size);
    dent.d_name[dno->name.size] = 0;

    return &dent;
}

 



void fsw_posix_rewinddir(struct fsw_posix_dir *dir)
{
    dir->shand.pos = 0;
}

 



int fsw_posix_closedir(struct fsw_posix_dir *dir)
{
    fsw_shandle_close(&dir->shand);
    fsw_free(dir);
    return 0;
}

 



fsw_status_t fsw_posix_open_dno(struct fsw_posix_volume *pvol, const char *path, int required_type, struct fsw_shandle *shand)
{
    fsw_status_t        status;
    struct fsw_dnode    *dno;
    struct fsw_dnode    *target_dno;
    struct fsw_string   lookup_path;

    lookup_path.type = FSW_STRING_TYPE_ISO88591;
    lookup_path.len  = strlen(path);
    lookup_path.size = lookup_path.len;
    lookup_path.data = (void *)path;

     
    status = fsw_dnode_lookup_path(pvol->vol->root, &lookup_path, '/', &dno);
    if (status) {
        fprintf(stderr, "fsw_posix_open_dno: fsw_dnode_lookup_path returned %d\n", status);
        return status;
    }

     
    status = fsw_dnode_resolve(dno, &target_dno);
    fsw_dnode_release(dno);
    if (status) {
        fprintf(stderr, "fsw_posix_open_dno: fsw_dnode_resolve returned %d\n", status);
        return status;
    }
    dno = target_dno;

     
    status = fsw_dnode_fill(dno);
    if (status) {
        fprintf(stderr, "fsw_posix_open_dno: fsw_dnode_fill returned %d\n", status);
        fsw_dnode_release(dno);
        return status;
    }
    if (dno->type != required_type) {
        fprintf(stderr, "fsw_posix_open_dno: dnode is not of the requested type\n");
        fsw_dnode_release(dno);
        return FSW_UNSUPPORTED;
    }

     
    status = fsw_shandle_open(dno, shand);
    if (status) {
        fprintf(stderr, "fsw_posix_open_dno: fsw_shandle_open returned %d\n", status);
    }
    fsw_dnode_release(dno);
    return status;
}

 




void fsw_posix_change_blocksize(struct fsw_volume *vol,
                                fsw_u32 old_phys_blocksize, fsw_u32 old_log_blocksize,
                                fsw_u32 new_phys_blocksize, fsw_u32 new_log_blocksize)
{
     
}

 




fsw_status_t fsw_posix_read_block(struct fsw_volume *vol, fsw_u32 phys_bno, void *buffer)
{
    struct fsw_posix_volume *pvol = (struct fsw_posix_volume *)vol->host_data;
    off_t           block_offset, seek_result;
    ssize_t         read_result;

    FSW_MSG_DEBUGV((FSW_MSGSTR("fsw_posix_read_block: %d  (%d)\n"), phys_bno, vol->phys_blocksize));

     
    block_offset = (off_t)phys_bno * vol->phys_blocksize;
    seek_result = lseek(pvol->fd, block_offset, SEEK_SET);
    if (seek_result != block_offset)
        return FSW_IO_ERROR;
    read_result = read(pvol->fd, buffer, vol->phys_blocksize);
    if (read_result != vol->phys_blocksize)
        return FSW_IO_ERROR;

    return FSW_SUCCESS;
}


 





 













 





 









 



 




















































 
