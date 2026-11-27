/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../boot/common_v2_memory.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PAGE 4096u
typedef struct { uintptr_t base; unsigned attrs[3]; int active,fail,lie,authorized; } Fixture;
static int alloc(void *ctx,size_t n,uintptr_t *p)
{
    Fixture *f=ctx; assert(!f->active && n==3*PAGE);
    void *m=aligned_alloc(PAGE,n); if(!m) return -1;
    f->active=1; f->base=*p=(uintptr_t)m;
    for(unsigned i=0;i<3;++i) f->attrs[i]=BOOT_V2_MEM_XP;
    return 0;
}
static int dealloc(void *ctx,uintptr_t p,size_t n)
{
    Fixture *f=ctx; assert(f->active && p==f->base && n==3*PAGE);
    for(unsigned i=0;i<3;++i) assert(f->attrs[i]==BOOT_V2_MEM_XP);
    free((void *)p); f->active=0; return 0;
}
static int change(void *ctx,uintptr_t p,size_t n,unsigned bits,int set)
{
    Fixture *f=ctx; assert(f->active && p>=f->base && p+n<=f->base+3*PAGE);
    if(f->fail) return -1;
    for(size_t i=(p-f->base)/PAGE;i<(p-f->base+n)/PAGE;++i) {
        if(set) f->attrs[i]|=bits; else f->attrs[i]&=~bits;
        assert(f->attrs[i]&(BOOT_V2_MEM_XP|BOOT_V2_MEM_RO));
    }
    return 0;
}
static int set(void *c,uintptr_t p,size_t n,unsigned a) { return change(c,p,n,a,1); }
static int clear(void *c,uintptr_t p,size_t n,unsigned a) { return change(c,p,n,a,0); }
static int get(void *ctx,uintptr_t p,size_t n,unsigned *a)
{
    Fixture *f=ctx; size_t i=(p-f->base)/PAGE; *a=f->attrs[i];
    for(;i<(p-f->base+n)/PAGE;++i) if(f->attrs[i]!=*a) return -1;
    if(f->lie) *a^=BOOT_V2_MEM_RO;
    return 0;
}
static int auth(void *ctx,const void *p,size_t n)
{ (void)p; (void)n; return ((Fixture *)ctx)->authorized?0:-1; }
int main(void)
{
    Fixture f={0}; BootV2Memory m={0}; BootV2ElfHost h={0};
    BootV2MemoryBackend b={&f,alloc,dealloc,set,clear,get};
    assert(boot_v2_memory_host(&m,&b,&f,NULL,&h)!=0);
    assert(boot_v2_memory_host(&m,&b,&f,auth,&h)==0);
    assert(h.authenticate(h.context,"x",1)!=0); f.authorized=1;
    assert(h.authenticate(h.context,"x",1)==0);
    assert(!h.reserve(h.context,1));
    m.maximum_bytes=0;
    assert(!h.reserve(h.context,3*PAGE)&&!f.active&&!m.allocated);
    m.maximum_bytes=3*PAGE-1;
    assert(!h.reserve(h.context,3*PAGE)&&!f.active&&!m.allocated);
    m.maximum_bytes=3*PAGE;
    unsigned char *p=h.reserve(h.context,3*PAGE); assert(p);
    assert(!h.reserve(h.context,3*PAGE)); assert(h.authenticate(h.context,"x",1)!=0);
    assert(h.protect(h.context,p,3*PAGE,0)==0);
    assert(h.protect(h.context,p,PAGE,BOOT_V2_ELF_R|BOOT_V2_ELF_X)==0);
    assert(h.protect(h.context,p+PAGE,PAGE,BOOT_V2_ELF_R)==0);
    assert(h.protect(h.context,p+2*PAGE,PAGE,BOOT_V2_ELF_R|BOOT_V2_ELF_W)==0);
    assert(f.attrs[0]==BOOT_V2_MEM_RO && f.attrs[1]==(BOOT_V2_MEM_RO|BOOT_V2_MEM_XP) && f.attrs[2]==BOOT_V2_MEM_XP);
    assert(h.protect(h.context,p,PAGE,BOOT_V2_ELF_W|BOOT_V2_ELF_X)!=0);
    assert(h.protect(h.context,p+1,PAGE,BOOT_V2_ELF_R)!=0);
    assert(h.protect(h.context,p+3*PAGE,PAGE,BOOT_V2_ELF_R)!=0);
    f.lie=1; assert(h.protect(h.context,p,PAGE,BOOT_V2_ELF_R)!=0); f.lie=0;
    f.fail=1; assert(h.protect(h.context,p,PAGE,BOOT_V2_ELF_R)!=0);
    h.release(h.context,p,3*PAGE); assert(m.quarantined && m.allocated && f.active);
    assert(!h.reserve(h.context,3*PAGE)); f.fail=0;
    h.release(h.context,p,3*PAGE); assert(!m.quarantined && !m.allocated && !f.active);
    puts("BOOT verified page permission transitions and quarantine PASS"); return 0;
}
