/*
 * Small POSIX FSW root-directory traversal test utility. Its presence is
 * test support, not evidence that any filesystem driver has been authenticated
 * or successfully executed by physical UEFI firmware.
 */
 




 































#include "fsw_posix.h"


extern struct fsw_fstype_table FSW_FSTYPE_TABLE_NAME(reiserfs);
extern struct fsw_fstype_table FSW_FSTYPE_TABLE_NAME(iso9660);
extern struct fsw_fstype_table FSW_FSTYPE_TABLE_NAME(hfs);
extern struct fsw_fstype_table FSW_FSTYPE_TABLE_NAME(ext2);
extern struct fsw_fstype_table FSW_FSTYPE_TABLE_NAME(ext4);

int main(int argc, char **argv)
{
    struct fsw_posix_volume *vol;
    struct fsw_posix_dir *dir;
    struct dirent *dent;

    if (argc != 2) {
        fprintf(stderr, "Usage: lsroot <file/device>\n");
        return 1;
    }

     
     
    vol = fsw_posix_mount(argv[1], &FSW_FSTYPE_TABLE_NAME(FSTYPE));
    if (vol == NULL) {
        fprintf(stderr, "Mounting failed.\n");
        return 1;
    }
     
    dir = fsw_posix_opendir(vol, "/");
    if (dir == NULL) {
        fprintf(stderr, "opendir call failed.\n");
        return 1;
    }
    while ((dent = fsw_posix_readdir(dir)) != NULL) {
        fprintf(stderr, "- %s\n", dent->d_name);
    }
    fsw_posix_closedir(dir);
    fsw_posix_unmount(vol);

    return 0;
}

 
