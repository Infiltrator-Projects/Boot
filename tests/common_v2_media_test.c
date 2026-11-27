/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise BOOT's actual ELF mapper and descriptor/storage boundary against
 * independently built firmware providers and actual on-disk namespaces.
 * This Linux harness supplies memory protection and disposable-file I/O;
 * it is not a substitute for the separate OVMF qualification. */
#define _GNU_SOURCE
#include "../boot/common_v2_module.h"
#include "../boot/common_v2_host.h"
#include "../boot/common_v2_trust.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct {int fd;uint64_t bytes;uint32_t sector;} Media;
static unsigned char *load(const char *path,size_t *n)
{
    FILE *f=fopen(path,"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long len=ftell(f);assert(len>0);
    rewind(f);*n=(size_t)len;unsigned char *p=malloc(*n);assert(p);
    assert(fread(p,1,*n,f)==*n);fclose(f);return p;
}
static int auth(void *ctx,const void *p,size_t n)
{
    unsigned char digest[32];boot_v2_sha256(p,n,digest);
    return memcmp(ctx,digest,32)?-1:0;
}
static void *reserve(void *ctx,size_t n)
{
    (void)ctx;void *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    return p==MAP_FAILED?NULL:p;
}
static int protect(void *ctx,void *p,size_t n,unsigned flags)
{
    (void)ctx;
    return mprotect(p,n,((flags&4)?PROT_READ:0)|((flags&2)?PROT_WRITE:0)|((flags&1)?PROT_EXEC:0));
}
static void release(void *ctx,void *p,size_t n)
{(void)ctx;assert(!munmap(p,n));}
static int geometry(void *ctx,BootV2Geometry *g)
{
    Media *m=ctx;
    *g=(BootV2Geometry){m->bytes/m->sector-1,1,m->sector,m->sector,1,1};
    return 0;
}
static int read_media(void *ctx,uint32_t media_id,uint64_t off,void *p,uint32_t n)
{
    Media *m=ctx;
    if(media_id!=1||off>m->bytes||n>m->bytes-off)return -1;
    return pread(m->fd,p,n,(off_t)off)==(ssize_t)n?0:-1;
}
static void *allocate(void *ctx,size_t n){(void)ctx;return malloc(n);}
static void release_pool(void *ctx,void *p){(void)ctx;free(p);}
int main(int argc,char **argv)
{
    assert(argc==3 || argc==7);size_t n,want_n=0;
    unsigned char *elf=load(argv[1],&n),*want=argc==7?load(argv[4],&want_n):NULL,digest[32];
    BootV2ElfImage image={0};BootV2ModuleView module={0};
    BootV2ElfHost elf_host={digest,auth,reserve,protect,release};
    boot_v2_sha256(elf,n,digest);
    if(boot_v2_elf_load(&elf_host,elf,n,&image)){
        fprintf(stderr,"BOOT ELF mapper rejected real provider %s\n",argv[1]);return 1;
    }
    free(elf);
    assert(boot_v2_module_inspect(&image,argv[2],&module)==IFS_FS_OK);
    const IfsFsCoreOpsV2 *core=boot_v2_module_readable_core(&module);
    if(argc==3){
        printf("BOOT %s mapped and inspected: %s; no media qualification claimed\n",
               argv[2],core?"readable namespace advertised":"no readable namespace");
        boot_v2_elf_unload(&elf_host,&image);
        return 0;
    }
    assert(core);
    Media media={.fd=open(argv[3],O_RDONLY),.sector=(uint32_t)strtoul(argv[6],NULL,10)};
    struct stat st;assert(media.fd>=0&&!fstat(media.fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0);
    media.bytes=(uint64_t)st.st_size;assert(media.sector&&!(media.bytes%media.sector));
    BootV2Backend backend={.context=&media,.geometry=geometry,.read=read_media,.allocate=allocate,.release=release_pool};
    BootV2Storage storage={0};IfsFsHostRuntimeV2 host={0};void *volume=NULL;uint32_t variant=0;
    assert(!boot_v2_storage_init(&storage,&backend,1024*1024,0));
    assert(boot_v2_host_init(&host,&storage)==IFS_FS_OK);
    assert(core->probe(&host,&variant)==IFS_FS_OK);
    IfsFsVolumeInfoV2 info={.structure_bytes=sizeof(info)};
    assert(core->mount(&host,&volume,&info)==IFS_FS_OK&&volume);
    assert(info.variant==variant&&info.total_bytes&&info.total_bytes<=media.bytes&&info.free_bytes<=info.total_bytes);
    char name[4096];IfsFsEntryV2 entry={.structure_bytes=sizeof(entry),.name=name,.name_capacity=sizeof(name)};
    assert(core->stat(volume,"/",&entry)==IFS_FS_OK&&entry.type==IFS_FS_ENTRY_DIRECTORY);
    assert(core->stat(volume,argv[5],&entry)==IFS_FS_OK&&entry.type==IFS_FS_ENTRY_FILE&&entry.size_bytes==want_n);
    assert(argv[5][0]=='/'&&!strchr(argv[5]+1,'/'));
    unsigned found=0,ended=0;
    for(uint32_t i=0;i<10000;i++){
        entry=(IfsFsEntryV2){.structure_bytes=sizeof(entry),.name=name,.name_capacity=sizeof(name)};
        int rc=core->directory_entry(volume,"/",i,&entry);
        if(rc==IFS_FS_END){ended=1;break;}
        assert(rc==IFS_FS_OK&&entry.name_bytes<sizeof(name)&&!name[entry.name_bytes]);
        if(!strcmp(name,argv[5]+1)){assert(entry.size_bytes==want_n);found++;}
    }
    assert(found==1&&ended);
    unsigned char buf[4093];
    for(size_t off=0;off<want_n;){
        uint32_t len=(uint32_t)(want_n-off>sizeof(buf)?sizeof(buf):want_n-off);
        assert(core->read(volume,argv[5],off,buf,len)==IFS_FS_OK);
        assert(!memcmp(want+off,buf,len));off+=len;
    }
    assert(core->stat(volume,"/missing-v2-qualifier-file",&entry)!=IFS_FS_OK);
    assert(core->unmount(volume)==IFS_FS_OK);
    assert(!storage.allocations);
    boot_v2_storage_close(&storage);close(media.fd);boot_v2_elf_unload(&elf_host,&image);free(want);
    printf("BOOT real %s probe/mount/list/stat/read/unmount PASS, sector=%u\n",argv[2],media.sector);
    return 0;
}
