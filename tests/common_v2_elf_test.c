/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "../boot/common_v2_elf.h"
#include <assert.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static int trusted=1, fail_protect=0, allocated=0;
/* Test seam only: production requires actual signature/trust verification. */
static int authenticate(void *ctx, const void *data, size_t size)
{ (void)ctx; (void)data; (void)size; return trusted?0:-1; }
static void *reserve(void *ctx, size_t n)
{
    (void)ctx;
    void *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(p==MAP_FAILED) return NULL;
    ++allocated; return p;
}
static int protect(void *ctx, void *p, size_t n, unsigned flags)
{
    (void)ctx;
    if(fail_protect) return -1;
    assert((flags & 3)!=3);
    return mprotect(p,n,((flags&4)?PROT_READ:0)|((flags&2)?PROT_WRITE:0)|((flags&1)?PROT_EXEC:0));
}
static void release(void *ctx, void *p, size_t n)
{ (void)ctx; assert(munmap(p,n)==0); --allocated; }
static void reject(const BootV2ElfHost *h, unsigned char *data, size_t n)
{
    BootV2ElfImage out={0};
    assert(boot_v2_elf_load(h,data,n,&out)!=0);
    assert(!out.base && !out.bytes && !out.entry && !allocated);
}
int main(int argc, char **argv)
{
    assert(argc==2); FILE *f=fopen(argv[1],"rb"); assert(f);
    assert(fseek(f,0,SEEK_END)==0); long length=ftell(f); assert(length>0);
    rewind(f); size_t n=(size_t)length; unsigned char *data=malloc(n); assert(data);
    assert(fread(data,1,n,f)==n); fclose(f);
    BootV2ElfHost h={NULL,authenticate,reserve,protect,release}; BootV2ElfImage out={0};
    assert(boot_v2_elf_load(&h,data,n,&out)==0);
    const unsigned *(*entry)(void); memcpy(&entry,&out.entry,sizeof(entry));
    assert(*entry()==0x12345678u); boot_v2_elf_unload(&h,&out); assert(!allocated);
    trusted=0; reject(&h,data,n); trusted=1;
    fail_protect=1; reject(&h,data,n); fail_protect=0;
    reject(&h,data,10); reject(&h,data,n/2);
    Elf64_Ehdr *eh=(void *)data; Elf64_Half machine=eh->e_machine;
    eh->e_machine=EM_AARCH64; reject(&h,data,n); eh->e_machine=machine;
    Elf64_Phdr *ph=(void *)(data+eh->e_phoff);
    for(unsigned i=0;i<eh->e_phnum;++i) if(ph[i].p_type==PT_LOAD) {
        Elf64_Word flags=ph[i].p_flags; ph[i].p_flags=PF_R|PF_W|PF_X;
        reject(&h,data,n); ph[i].p_flags=flags; break;
    }
    for(unsigned i=0;i<eh->e_phnum;++i) if(ph[i].p_type==PT_DYNAMIC) {
        Elf64_Dyn *d=(void *)(data+ph[i].p_offset);
        Elf64_Sxword tag=d[0].d_tag; d[0].d_tag=DT_NEEDED;
        reject(&h,data,n); d[0].d_tag=tag; break;
    }
    const char v2[]="infiltrator_filesystem_api_v2";
    unsigned changed=0;
    for(size_t i=0;i+sizeof(v2)<=n;++i) if(!memcmp(data+i,v2,sizeof(v2))) {
        data[i+sizeof(v2)-2]='1'; ++changed;
    }
    assert(changed); reject(&h,data,n);
    free(data); puts("BOOT restricted ELF load/relocate/protect/reject PASS"); return 0;
}
