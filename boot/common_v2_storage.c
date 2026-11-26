/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_storage.h"

static int power2(uint32_t n) { return n && !(n & (n - 1)); }
static int valid_geometry(const BootV2Geometry *g)
{
    return g->present == 1 && g->read_only <= 1 &&
        g->logical_bytes >= 512 && g->logical_bytes <= 65536 && power2(g->logical_bytes) &&
        g->physical_bytes >= g->logical_bytes && g->physical_bytes <= 65536 &&
        power2(g->physical_bytes) && !(g->physical_bytes % g->logical_bytes) &&
        g->last_block < UINT64_MAX / g->logical_bytes;
}
static int same_geometry(const BootV2Geometry *a, const BootV2Geometry *b)
{
    return a->media_id==b->media_id && a->last_block==b->last_block &&
        a->logical_bytes==b->logical_bytes && a->physical_bytes==b->physical_bytes &&
        a->read_only==b->read_only && a->present==b->present;
}
int boot_v2_storage_init(BootV2Storage *s, const BootV2Backend *b, uint32_t max, int write)
{
    BootV2Geometry g={0};
    if (!s || s->active || s->allocations || !b || !b->geometry || !b->read || !b->allocate || !b->release ||
        b->geometry(b->context,&g) || !valid_geometry(&g) || max<g.logical_bytes) return -1;
    if (write && (g.read_only || !b->write || !b->flush || !b->check_write_lease ||
        b->check_write_lease(b->context,g.media_id))) return -1;
    s->backend=*b; s->geometry=g;
    s->volume_bytes=(g.last_block+1)*g.logical_bytes;
    s->maximum_transfer=max-max%g.logical_bytes;
    s->writable=!!write; s->stale=0; s->active=1;
    return 0;
}
void boot_v2_storage_close(BootV2Storage *s)
{
    /* Allocation release remains available for late cleanup, but no storage
     * or new allocations are permitted after the volume's lifetime ends. */
    if (s) { s->active=0; s->writable=0; }
}
static int live(BootV2Storage *s, int write)
{
    BootV2Geometry now={0};
    if (!s || !s->active || s->stale) return 0;
    if (s->backend.geometry(s->backend.context,&now) || !valid_geometry(&now) ||
        !same_geometry(&s->geometry,&now)) {
        /* Once the mounted media identity is lost, never silently resume
         * the old filesystem session even if an identifier is reused. */
        s->stale=1; return 0;
    }
    return !write || (s->writable && !now.read_only &&
        !s->backend.check_write_lease(s->backend.context,now.media_id));
}
static int range(const BootV2Storage *s, uint64_t off, const void *p, uint32_t n)
{
    return (!n || p) && off<=s->volume_bytes && n<=s->volume_bytes-off &&
        n<=s->maximum_transfer && !(off%s->geometry.logical_bytes) && !(n%s->geometry.logical_bytes);
}
int boot_v2_storage_read(void *ctx, uint64_t off, void *p, uint32_t n)
{
    BootV2Storage *s=ctx;
    if (!live(s,0) || !range(s,off,p,n)) return -1;
    return n ? s->backend.read(s->backend.context,s->geometry.media_id,off,p,n) : 0;
}
int boot_v2_storage_write(void *ctx, uint64_t off, const void *p, uint32_t n)
{
    BootV2Storage *s=ctx;
    if (!live(s,1) || !range(s,off,p,n)) return -1;
    return n ? s->backend.write(s->backend.context,s->geometry.media_id,off,p,n) : 0;
}
int boot_v2_storage_flush(void *ctx)
{
    BootV2Storage *s=ctx;
    return live(s,1) ? s->backend.flush(s->backend.context,s->geometry.media_id) : -1;
}
void *boot_v2_storage_allocate(void *ctx, size_t n)
{
    BootV2Storage *s=ctx;
    void *p;
    if (!s || !s->active || s->stale || !n || s->allocations==SIZE_MAX) return NULL;
    p=s->backend.allocate(s->backend.context,n);
    if (p) ++s->allocations;
    return p;
}
void boot_v2_storage_release(void *ctx, void *p)
{
    BootV2Storage *s=ctx;
    if (s && s->backend.release && p && s->allocations) {
        s->backend.release(s->backend.context,p); --s->allocations;
    }
}
