/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../boot/common_v2_storage.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    BootV2Geometry geometry;
    unsigned char media[16384];
    int lease, failure, reads, writes, flushes, allocations;
} Fixture;
static int geo(void *c,BootV2Geometry *g) { *g=((Fixture *)c)->geometry; return 0; }
static int rd(void *c,uint32_t id,uint64_t off,void *p,uint32_t n)
{ Fixture *f=c; assert(id==f->geometry.media_id && off+n<=sizeof(f->media)); ++f->reads; if(f->failure) return -1; memcpy(p,f->media+off,n); return 0; }
static int wr(void *c,uint32_t id,uint64_t off,const void *p,uint32_t n)
{ Fixture *f=c; assert(id==f->geometry.media_id && off+n<=sizeof(f->media)); ++f->writes; if(f->failure) return -1; memcpy(f->media+off,p,n); return 0; }
static int flush(void *c,uint32_t id)
{ Fixture *f=c; assert(id==f->geometry.media_id); ++f->flushes; return f->failure?-1:0; }
static int lease(void *c,uint32_t id)
{ Fixture *f=c; return f->lease && id==f->geometry.media_id?0:-1; }
static void *alloc(void *c,size_t n) { void *p=malloc(n); if(p) ++((Fixture *)c)->allocations; return p; }
static void release(void *c,void *p) { --((Fixture *)c)->allocations; free(p); }
int main(void)
{
    Fixture f={.geometry={31,7,512,4096,1,0}};
    BootV2Backend b={&f,geo,rd,wr,flush,lease,alloc,release}; BootV2Storage s={0};
    unsigned char p[4096]; memset(p,0x57,sizeof(p));
    assert(boot_v2_storage_init(&s,&b,4100,1)!=0 && !s.active);
    assert(boot_v2_storage_init(&s,&b,4100,0)==0);
    assert(s.volume_bytes==16384 && s.maximum_transfer==4096 && !s.writable);
    assert(boot_v2_storage_init(&s,&b,4100,0)!=0);
    assert(boot_v2_storage_read(&s,12288,p,4096)==0 && f.reads==1);
    assert(boot_v2_storage_read(&s,16384,NULL,0)==0 && f.reads==1);
    assert(boot_v2_storage_read(&s,16384,p,512)!=0);
    assert(boot_v2_storage_read(&s,UINT64_MAX-511,p,512)!=0);
    assert(boot_v2_storage_read(&s,1,p,512)!=0);
    assert(boot_v2_storage_read(&s,0,p,511)!=0);
    assert(boot_v2_storage_read(&s,0,p,4608)!=0);
    assert(boot_v2_storage_read(&s,0,NULL,512)!=0 && f.reads==1);
    f.lease=1; assert(boot_v2_storage_write(&s,0,p,512)!=0 && !f.writes);
    assert(boot_v2_storage_flush(&s)!=0 && !f.flushes);
    f.failure=1; assert(boot_v2_storage_read(&s,0,p,512)!=0 && f.reads==2); f.failure=0;
    void *a=boot_v2_storage_allocate(&s,512); assert(a && f.allocations==1);
    ++f.geometry.media_id; assert(boot_v2_storage_read(&s,0,p,512)!=0 && f.reads==2); --f.geometry.media_id;
    assert(boot_v2_storage_read(&s,0,p,512)!=0 && f.reads==2 && s.stale);
    boot_v2_storage_close(&s); assert(!boot_v2_storage_allocate(&s,1));
    assert(boot_v2_storage_init(&s,&b,4096,0)!=0);
    assert(boot_v2_storage_read(&s,0,p,512)!=0); boot_v2_storage_release(&s,a); assert(!f.allocations);
    assert(boot_v2_storage_init(&s,&b,4096,0)==0);
    f.geometry.present=0; assert(boot_v2_storage_read(&s,0,p,512)!=0); f.geometry.present=1;
    boot_v2_storage_close(&s); assert(boot_v2_storage_init(&s,&b,4096,0)==0);
    f.geometry.physical_bytes=512; assert(boot_v2_storage_read(&s,0,p,512)!=0); f.geometry.physical_bytes=4096;
    boot_v2_storage_close(&s);
    assert(boot_v2_storage_init(&s,&b,4096,1)==0 && s.writable);
    memset(p,0xab,sizeof(p)); assert(boot_v2_storage_write(&s,4096,p,4096)==0 && f.writes==1);
    assert(!memcmp(f.media+4096,p,4096)); assert(boot_v2_storage_flush(&s)==0 && f.flushes==1);
    f.lease=0; assert(boot_v2_storage_write(&s,0,p,512)!=0 && f.writes==1);
    assert(boot_v2_storage_flush(&s)!=0 && f.flushes==1);
    f.lease=1; f.geometry.read_only=1; assert(boot_v2_storage_write(&s,0,p,512)!=0);
    f.geometry.read_only=0; boot_v2_storage_close(&s);
    assert(boot_v2_storage_init(&s,&b,4096,1)==0); f.failure=1; assert(boot_v2_storage_write(&s,0,p,512)!=0 && f.writes==2);
    assert(boot_v2_storage_flush(&s)!=0 && f.flushes==2); f.failure=0;
    boot_v2_storage_close(&s); f.geometry.last_block=UINT64_MAX;
    assert(boot_v2_storage_init(&s,&b,4096,0)!=0); f.geometry.last_block=3;
    f.geometry.logical_bytes=4096; f.geometry.physical_bytes=4096;
    assert(boot_v2_storage_init(&s,&b,4095,0)!=0);
    assert(boot_v2_storage_init(&s,&b,4096,0)==0 && s.volume_bytes==16384);
    assert(boot_v2_storage_read(&s,4096,p,4096)==0); assert(boot_v2_storage_read(&s,512,p,512)!=0);
    boot_v2_storage_close(&s); f.geometry.physical_bytes=8193;
    assert(boot_v2_storage_init(&s,&b,4096,0)!=0);
    puts("BOOT bounded storage, media lifetime, lease and failure paths PASS"); return 0;
}
