 




 































#ifndef _FSW_POSIX_H_
#define _FSW_POSIX_H_

#include "fsw_core.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/dir.h>


 



struct fsw_posix_volume {
    struct fsw_volume           *vol;            

    int                         fd;              

};

 



struct fsw_posix_file {
    struct fsw_posix_volume     *pvol;           

    struct fsw_shandle          shand;           

};

 



struct fsw_posix_dir {
    struct fsw_posix_volume     *pvol;           

    struct fsw_shandle          shand;           

};


 

struct fsw_posix_volume * fsw_posix_mount(const char *path, struct fsw_fstype_table *fstype_table);
int fsw_posix_unmount(struct fsw_posix_volume *pvol);

struct fsw_posix_file * fsw_posix_open(struct fsw_posix_volume *pvol, const char *path, int flags, mode_t mode);
ssize_t fsw_posix_read(struct fsw_posix_file *file, void *buf, size_t nbytes);
off_t fsw_posix_lseek(struct fsw_posix_file *file, off_t offset, int whence);
int fsw_posix_close(struct fsw_posix_file *file);

struct fsw_posix_dir * fsw_posix_opendir(struct fsw_posix_volume *pvol, const char *path);
struct dirent * fsw_posix_readdir(struct fsw_posix_dir *dir);
void fsw_posix_rewinddir(struct fsw_posix_dir *dir);
int fsw_posix_closedir(struct fsw_posix_dir *dir);


#endif
