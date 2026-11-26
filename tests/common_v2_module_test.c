/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "../boot/common_v2_module.h"
#include "../boot/common_v2_host.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

static int auth(void *c,const void *p,size_t n)
{ (void)c; (void)p; (void)n; return 0; } /* test seam, not production trust */
static void *reserve(void *c,size_t n)
{
    (void)c;
    void *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    return p==MAP_FAILED?NULL:p;
}
static int protect(void *c,void *p,size_t n,unsigned flags)
{
    (void)c;
    return mprotect(p,n,((flags&4)?PROT_READ:0)|((flags&2)?PROT_WRITE:0)|((flags&1)?PROT_EXEC:0));
}
static void release(void *c,void *p,size_t n)
{ (void)c; assert(munmap(p,n)==0); }
static int geo(void *c,BootV2Geometry *g)
{ (void)c; *g=(BootV2Geometry){7,5,512,4096,1,0}; return 0; }
static int read_disk(void *c,uint32_t id,uint64_t off,void *p,uint32_t n)
{ (void)c; (void)id; (void)off; (void)p; (void)n; return -1; }
static void *allocate(void *c,size_t n) { (void)c; return malloc(n); }
static void free_mem(void *c,void *p) { (void)c; free(p); }
int main(int argc,char **argv)
{
    BootV2ElfHost h={NULL,auth,reserve,protect,release};
    BootV2Backend b={.geometry=geo,.read=read_disk,.allocate=allocate,.release=free_mem};
    BootV2Storage storage={0}; IfsFsHostRuntimeV2 host={0};
    assert(boot_v2_storage_init(&storage,&b,4096,0)==0);
    assert(boot_v2_host_init(&host,&storage)==IFS_FS_OK && ifs_fs_host_v2_valid(&host));
    assert(host.capabilities==IFS_FS_HOST_READ_ONLY && !host.write_at && !host.flush);
    assert(host.logical_sector_bytes==512 && host.physical_sector_bytes==4096);
    boot_v2_storage_close(&storage);
    assert(argc==11);
    for(int i=1;i<argc;++i) {
        FILE *f=fopen(argv[i],"rb"); assert(f);
        assert(fseek(f,0,SEEK_END)==0); long len=ftell(f); assert(len>0); rewind(f);
        unsigned char *p=malloc((size_t)len); assert(p);
        assert(fread(p,1,(size_t)len,f)==(size_t)len); fclose(f);
        BootV2ElfImage image={0}; BootV2ModuleView view={0};
        assert(boot_v2_elf_load(&h,p,(size_t)len,&image)==0); free(p);
        if(i==1) {
            assert(boot_v2_module_inspect(&image,"wrong",&view)!=IFS_FS_OK && !view.api);
            assert(boot_v2_module_inspect(&image,"fixture",&view)==IFS_FS_OK);
            assert(view.api && view.groups[IFS_FS_GROUP_CORE]);
            assert(!boot_v2_module_readable_core(&view)); /* PROBE alone is not bootable */
        } else assert(boot_v2_module_inspect(&image,"fixture",&view)!=IFS_FS_OK && !view.api);
        boot_v2_elf_unload(&h,&image);
        assert(!boot_v2_module_readable_core(&view));
    }
    puts("BOOT canonical V2 host and bounded descriptor validation PASS"); return 0;
}
