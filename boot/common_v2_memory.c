/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_memory.h"
#define PAGE ((size_t)4096)
#define MASK (BOOT_V2_MEM_RP|BOOT_V2_MEM_RO|BOOT_V2_MEM_XP)

static int authenticate(void *ctx,const void *p,size_t n)
{
    BootV2Memory *m=ctx;
    if(!m || m->allocated || m->quarantined || !p || !n) return -1;
    return m->authenticate(m->trust_context,p,n);
}
static int contains(const BootV2Memory *m,uintptr_t p,size_t n)
{
    return m && m->allocated && n && !(p%PAGE) && !(n%PAGE) &&
        p>=m->base && p-m->base<=m->bytes && n<=m->bytes-(p-m->base);
}
/* Transition through NX before allowing writes or changing read protection.
 * RX is granted only after RO is set. No transition grants W+X, and final
 * permissions are read back from the backend, not inferred from set success. */
static int apply(BootV2Memory *m,uintptr_t p,size_t n,unsigned want)
{
    BootV2MemoryBackend *b=&m->backend;
    unsigned actual=0;
    if(b->set(b->context,p,n,BOOT_V2_MEM_XP)) return -1;
    if(want&BOOT_V2_MEM_RO) {
        if(b->set(b->context,p,n,BOOT_V2_MEM_RO)) return -1;
    }
    if(!(want&BOOT_V2_MEM_RP)) {
        if(b->clear(b->context,p,n,BOOT_V2_MEM_RP)) return -1;
    }
    if(!(want&BOOT_V2_MEM_RO)) {
        if(b->clear(b->context,p,n,BOOT_V2_MEM_RO)) return -1;
    }
    if(want&BOOT_V2_MEM_RP) {
        if(b->set(b->context,p,n,BOOT_V2_MEM_RP)) return -1;
    }
    if(!(want&BOOT_V2_MEM_XP)) {
        if(b->clear(b->context,p,n,BOOT_V2_MEM_XP)) return -1;
    }
    if(b->get(b->context,p,n,&actual) || (actual&MASK)!=want) return -1;
    return 0;
}
static void release(void *ctx,void *p,size_t n)
{
    BootV2Memory *m=ctx;
    if(!m || !m->allocated || (uintptr_t)p!=m->base || n!=m->bytes) return;
    /* Firmware may clear freed pages. Restore RW/NX before releasing them.
     * If this cannot be proved, quarantine the reservation rather than hand
     * inaccessible memory to firmware or accidentally reuse a live mapping. */
    if(apply(m,m->base,m->bytes,BOOT_V2_MEM_XP) ||
       m->backend.free(m->backend.context,m->base,m->bytes)) {
        m->quarantined=1; return;
    }
    m->base=0; m->bytes=0; m->allocated=0; m->quarantined=0;
}
static void *reserve(void *ctx,size_t n)
{
    BootV2Memory *m=ctx; uintptr_t p=0;
    if(!m || m->allocated || m->quarantined || !n || n%PAGE ||
       m->backend.allocate(m->backend.context,n,&p)) return NULL;
    m->base=p; m->bytes=n; m->allocated=1;
    if(!p || p%PAGE || p>UINTPTR_MAX-n) {
        /* This violates the backend allocation contract; do not apply page
         * permissions to an invalid range. Return it through the allocator. */
        if(m->backend.free(m->backend.context,p,n)) m->quarantined=1;
        else { m->base=0; m->bytes=0; m->allocated=0; }
        return NULL;
    }
    if(apply(m,p,n,BOOT_V2_MEM_XP)) { release(m,(void *)p,n); return NULL; }
    return (void *)p;
}
static int protect(void *ctx,void *p,size_t n,unsigned flags)
{
    BootV2Memory *m=ctx; unsigned want;
    if(!contains(m,(uintptr_t)p,n) || m->quarantined) return -1;
    switch(flags) {
    case 0: want=MASK; break;
    case BOOT_V2_ELF_R: want=BOOT_V2_MEM_RO|BOOT_V2_MEM_XP; break;
    case BOOT_V2_ELF_R|BOOT_V2_ELF_W: want=BOOT_V2_MEM_XP; break;
    case BOOT_V2_ELF_R|BOOT_V2_ELF_X: want=BOOT_V2_MEM_RO; break;
    default: return -1;
    }
    return apply(m,(uintptr_t)p,n,want);
}
int boot_v2_memory_host(BootV2Memory *m,const BootV2MemoryBackend *b,void *trust,
                        int (*auth)(void *,const void *,size_t),BootV2ElfHost *out)
{
    if(!m || m->allocated || m->quarantined || !b || !b->allocate || !b->free ||
       !b->set || !b->clear || !b->get || !auth || !out) return -1;
    m->backend=*b; m->trust_context=trust; m->authenticate=auth;
    *out=(BootV2ElfHost){m,authenticate,reserve,protect,release};
    return 0;
}
