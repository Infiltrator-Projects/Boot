 




 
































#define FSW_DEBUG_LEVEL 3

#include "fsw_posix.h"


 
 
extern struct fsw_fstype_table FSW_FSTYPE_TABLE_NAME(FSTYPE);

static struct fsw_fstype_table *fstypes[] = {
     
     
    &FSW_FSTYPE_TABLE_NAME(FSTYPE       ),
    NULL
};

static int listdir(struct fsw_posix_volume *vol, char *path, int level)
{
    struct fsw_posix_dir *dir;
    struct dirent *dent;
    int i;
    char subpath[4096];

    dir = fsw_posix_opendir(vol, path);
    if (dir == NULL) {
        fprintf(stderr, "opendir(%s) call failed.\n", path);
        return 1;
    }
    while ((dent = fsw_posix_readdir(dir)) != NULL) {
        for (i = 0; i < level*2; i++)
            fputc(' ', stderr);
        fprintf(stderr, "%d  %s\n", dent->d_type, dent->d_name);

        if (dent->d_type == DT_DIR) {
            snprintf(subpath, 4095, "%s%s/", path, dent->d_name);
            listdir(vol, subpath, level + 1);
        }
    }

    fsw_posix_closedir(dir);

    return 0;
}

static int catfile(struct fsw_posix_volume *vol, char *path)
{
    struct fsw_posix_file *file;
    int r;
    char buf[256];

    file = fsw_posix_open(vol, path, 0, 0);
    if (file == NULL) {
        fprintf(stderr, "open(%s) call failed.\n", path);
        return 1;
    }

    while ((r=fsw_posix_read(file, buf, sizeof(buf))) > 0)
    {
        int i;
        for (i=0; i<r; i++)
        {
           printf("%c", buf[i]);
        }
    }
    fsw_posix_close(file);

    return 0;
}

int main(int argc, char **argv)
{
    struct fsw_posix_volume *vol;
    int i;

    if (argc != 2) {
        fprintf(stderr, "Usage: lslr <file/device>\n");
        return 1;
    }

    for (i = 0; fstypes[i]; i++) {
        vol = fsw_posix_mount(argv[1], fstypes[i]);
        if (vol != NULL) {
            fprintf(stderr, "Mounted as '%s'.\n", fstypes[i]->name.data);
            break;
        }
    }
    if (vol == NULL) {
        fprintf(stderr, "Mounting failed.\n");
        return 1;
    }

    listdir(vol, "/boot/", 0);
    catfile(vol, "/boot/testfile.txt");

    fsw_posix_unmount(vol);

    return 0;
}

 
