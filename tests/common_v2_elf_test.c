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
static unsigned char *read_fixture(const char *path, size_t *n)
{
    FILE *f=fopen(path,"rb"); assert(f);
    assert(fseek(f,0,SEEK_END)==0); long length=ftell(f); assert(length>0);
    rewind(f); *n=(size_t)length; unsigned char *data=malloc(*n); assert(data);
    assert(fread(data,1,*n,f)==*n); fclose(f); return data;
}
int main(int argc, char **argv)
{
    assert(argc==4); size_t n, mixed_n, relro_n; unsigned char *data=read_fixture(argv[1],&n);
    unsigned char *mixed=read_fixture(argv[2],&mixed_n);
    unsigned char *relro=read_fixture(argv[3],&relro_n);
    BootV2ElfHost h={NULL,authenticate,reserve,protect,release}; BootV2ElfImage out={0};
    assert(boot_v2_elf_load(&h,data,n,&out)==0);
    const unsigned *(*entry)(void); memcpy(&entry,&out.entry,sizeof(entry));
    assert(*entry()==0x12345678u);
    assert(boot_v2_elf_contains(&out,(uintptr_t)out.entry,1,BOOT_V2_ELF_X,BOOT_V2_ELF_W));
    assert(!boot_v2_elf_contains(&out,(uintptr_t)out.entry,1,BOOT_V2_ELF_W,0));
    assert(boot_v2_elf_contains(&out,(uintptr_t)entry(),sizeof(unsigned),BOOT_V2_ELF_R,BOOT_V2_ELF_W|BOOT_V2_ELF_X));
    assert(!boot_v2_elf_contains(&out,(uintptr_t)entry(),sizeof(unsigned),BOOT_V2_ELF_X,0));
    assert(!boot_v2_elf_contains(&out,(uintptr_t)out.base+out.bytes,1,BOOT_V2_ELF_R,0));
    assert(!boot_v2_elf_contains(&out,UINTPTR_MAX,2,BOOT_V2_ELF_R,0));
    assert(!boot_v2_elf_contains(&out,(uintptr_t)out.entry,0,BOOT_V2_ELF_X,0));
    for(unsigned i=0;i<out.region_count;++i) {
        BootV2ElfRegion *r=&out.regions[i];
        if(r->bytes%4096 && i+1<out.region_count) {
            uintptr_t padding=(uintptr_t)out.base+r->offset+r->bytes;
            assert(!boot_v2_elf_contains(&out,padding,1,BOOT_V2_ELF_R,0));
        }
    }
    BootV2ElfImage second={0}; assert(boot_v2_elf_load(&h,data,n,&second)==0);
    assert(second.base!=out.base && allocated==2);
    const unsigned *(*entry2)(void); memcpy(&entry2,&second.entry,sizeof(entry2));
    boot_v2_elf_unload(&h,&out); assert(!out.region_count && allocated==1 && *entry2()==0x12345678u);
    boot_v2_elf_unload(&h,&second); assert(!allocated);
    reject(&h,mixed,mixed_n); free(mixed);
    assert(boot_v2_elf_load(&h,relro,relro_n,&out)==0);
    memcpy(&entry,&out.entry,sizeof(entry)); assert(*entry()==0x12345678u);
    assert(boot_v2_elf_contains(&out,(uintptr_t)entry(),sizeof(unsigned),BOOT_V2_ELF_R,BOOT_V2_ELF_W|BOOT_V2_ELF_X));
    boot_v2_elf_unload(&h,&out); assert(!allocated);
    /* Extend the same RELRO past its owning mapped page: must fail. */
    Elf64_Ehdr *re=(void *)relro; Elf64_Phdr *rp=(void *)(relro+re->e_phoff);
    for(unsigned i=0;i<re->e_phnum;++i) if(rp[i].p_type==PT_GNU_RELRO) {
        rp[i].p_memsz+=4096; reject(&h,relro,relro_n); break;
    }
    free(relro);
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
    Elf64_Shdr *sh=(void *)(data+eh->e_shoff); unsigned relocations=0;
    for(unsigned i=0;i<eh->e_shnum;++i) if(sh[i].sh_type==SHT_RELA && sh[i].sh_size) {
        Elf64_Rela *r=(void *)(data+sh[i].sh_offset); Elf64_Rela saved=*r;
        r->r_info=ELF64_R_INFO(0,R_X86_64_IRELATIVE); reject(&h,data,n); *r=saved;
        r->r_offset=UINT64_MAX-7; reject(&h,data,n); *r=saved;
        r->r_addend=-1; reject(&h,data,n); *r=saved;
        ++relocations; break;
    }
    assert(relocations);
    unsigned symbols=0;
    for(unsigned i=0;i<eh->e_shnum;++i) if(sh[i].sh_type==SHT_DYNSYM) {
        unsigned char *null_symbol=data+sh[i].sh_offset;
        Elf64_Shdr *strings=&sh[sh[i].sh_link];
        unsigned char *names=data+strings->sh_offset;
        size_t last=strings->sh_size-1;
        assert(!null_symbol[0]&&!names[0]&&!names[last]);
        null_symbol[0]=1; reject(&h,data,n); null_symbol[0]=0;
        names[0]=1; reject(&h,data,n); names[0]=0;
        names[last]=1; reject(&h,data,n); names[last]=0;
        ++symbols; break;
    }
    assert(symbols);
    const char v2[]="infiltrator_filesystem_api_v2";
    unsigned changed=0;
    for(size_t i=0;i+sizeof(v2)<=n;++i) if(!memcmp(data+i,v2,sizeof(v2))) {
        data[i+sizeof(v2)-2]='1'; ++changed;
    }
    assert(changed); reject(&h,data,n);
    free(data); puts("BOOT restricted ELF load/relocate/protect/reject PASS"); return 0;
}
