/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Deliberately bounded ELF64 loader for the no-import SysV module profile.
 * Source bytes remain immutable while relocating a private image. This loader
 * supplies no filesystem interpretation and never resolves host symbols. */
#include "so_elf.h"

typedef struct {
    unsigned char ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} Ehdr;
typedef struct {
    uint32_t type, flags;
    uint64_t offset, address, physical, file_bytes, memory_bytes, align;
} Phdr;
typedef struct {
    uint32_t name, type;
    uint64_t flags, address, offset, bytes;
    uint32_t link, info;
    uint64_t align, entry_bytes;
} Shdr;
typedef struct {
    uint32_t name;
    uint8_t info, other;
    uint16_t section;
    uint64_t value, bytes;
} Sym;
typedef struct { uint64_t target, info; int64_t addend; } Rela;
typedef struct { int64_t tag; uint64_t value; } Dyn;
typedef struct {
    Ehdr elf;
    Phdr loads[BOOT_SO_MAX_SEGMENTS];
    uint32_t count;
    Shdr symbols, strings, relocations;
    uint64_t entry, image_bytes;
} Plan;

static void copy(void *destination, const void *source, size_t bytes) {
    unsigned char *d = destination;
    const unsigned char *s = source;
    while (bytes--) *d++ = *s++;
}
static void zero(void *destination, size_t bytes) {
    unsigned char *d = destination;
    while (bytes--) *d++ = 0;
}
static int equal(const void *a, const void *b, size_t bytes) {
    const unsigned char *x=a, *y=b;
    while (bytes--) if (*x++ != *y++) return 0;
    return 1;
}
static int span(uint64_t offset, uint64_t bytes, uint64_t limit) {
    return offset <= limit && bytes <= limit-offset;
}
static int name_is(const unsigned char *strings, uint64_t bytes,
                    uint32_t offset, const char *name) {
    if (offset >= bytes) return 0;
    for (uint64_t i=offset; i<bytes; ++i, ++name) {
        if (strings[i] != (unsigned char)*name) return 0;
        if (!*name) return 1;
    }
    return 0;
}
static const Phdr *region(const Plan *p, uint64_t address, uint64_t bytes,
                           uint32_t required, uint32_t forbidden) {
    for (uint32_t i=0; i<p->count; ++i) {
        const Phdr *s=&p->loads[i];
        if (address>=s->address &&
            span(address-s->address, bytes, s->memory_bytes) &&
            (s->flags & required)==required && !(s->flags & forbidden)) return s;
    }
    return 0;
}
static int plan(const void *data, size_t bytes, Plan *p) {
    const unsigned char *source=data;
    Shdr names={0};
    Phdr dynamic={0};
    uint64_t dynamic_symbols=0, dynamic_strings=0, dynamic_strings_bytes=0;
    uint64_t dynamic_relocations=0, dynamic_relocation_bytes=0;
    int have_symbols=0, have_strings=0, have_relocations=0;
    if (!data || bytes<sizeof(Ehdr) || bytes>BOOT_SO_MAX_FILE) return -1;
    zero(p, sizeof(*p));
    copy(&p->elf, source, sizeof(p->elf));
    const Ehdr *e=&p->elf;
    if (!equal(e->ident, "\177ELF\2\1\1", 7) ||
        (e->ident[7]!=0 && e->ident[7]!=3) || e->ident[8] ||
        e->type!=3 || e->machine!=62 || e->version!=1 || e->flags || e->entry ||
        e->ehsize!=sizeof(Ehdr) || e->phentsize!=sizeof(Phdr) ||
        e->shentsize!=sizeof(Shdr) || !e->phnum || e->phnum>32u ||
        !e->shnum || e->shnum>256u || e->shstrndx>=e->shnum ||
        !span(e->phoff,(uint64_t)e->phnum*sizeof(Phdr),bytes) ||
        !span(e->shoff,(uint64_t)e->shnum*sizeof(Shdr),bytes)) return -1;

    for (uint32_t i=0; i<e->phnum; ++i) {
        Phdr s;
        copy(&s, source+e->phoff+(uint64_t)i*sizeof(s), sizeof(s));
        if (s.type==3 || s.type==7 || (s.type==0x6474e551u && (s.flags&1u))) return -1;
        if (s.type==2) {
            if (dynamic.type || !s.file_bytes || s.file_bytes%sizeof(Dyn) ||
                s.file_bytes>128u*sizeof(Dyn) || !span(s.offset,s.file_bytes,bytes)) return -1;
            dynamic=s;
        }
        if (s.type!=1) continue;
        if (p->count==BOOT_SO_MAX_SEGMENTS || !s.memory_bytes ||
            s.file_bytes>s.memory_bytes || !span(s.offset,s.file_bytes,bytes) ||
            !span(s.address,s.memory_bytes,BOOT_SO_MAX_IMAGE) ||
            !(s.flags&4u) || (s.flags&~7u) || (s.flags&3u)==3u ||
            s.align>4096u || (s.align && (s.align&(s.align-1u))) ||
            (s.address&4095u)!=(s.offset&4095u)) return -1;
        for (uint32_t j=0; j<p->count; ++j) {
            const Phdr *t=&p->loads[j];
            uint64_t s_end=(s.address+s.memory_bytes+4095u)&~UINT64_C(4095);
            uint64_t t_end=(t->address+t->memory_bytes+4095u)&~UINT64_C(4095);
            /* Even distinct byte ranges may not share a page across segments. */
            if ((s.address&~UINT64_C(4095))<t_end &&
                (t->address&~UINT64_C(4095))<s_end) return -1;
        }
        p->loads[p->count++]=s;
        if (s.address+s.memory_bytes>p->image_bytes)
            p->image_bytes=s.address+s.memory_bytes;
    }
    const Phdr *dynamic_segment=region(p,dynamic.address,dynamic.file_bytes,4,1);
    if (!p->count || !dynamic.type || !region(p,0,sizeof(Ehdr),4,3) ||
        !dynamic_segment || dynamic.address-dynamic_segment->address>dynamic_segment->file_bytes ||
        dynamic.file_bytes>dynamic_segment->file_bytes-(dynamic.address-dynamic_segment->address) ||
        dynamic.offset!=dynamic_segment->offset+dynamic.address-dynamic_segment->address) return -1;
    p->image_bytes=(p->image_bytes+4095u)&~UINT64_C(4095);
    copy(&names,source+e->shoff+(uint64_t)e->shstrndx*sizeof(Shdr),sizeof(names));
    if (names.type!=3 || !span(names.offset,names.bytes,bytes)) return -1;

    for (uint32_t i=1; i<e->shnum; ++i) {
        Shdr s;
        copy(&s,source+e->shoff+(uint64_t)i*sizeof(s),sizeof(s));
        if (s.type!=8 && !span(s.offset,s.bytes,bytes)) return -1;
        if (s.name>=names.bytes || s.type==9 || s.type==14 || s.type==15 ||
            s.type==16 || s.type==19 || (s.flags&0x400u)) return -1;
        if ((s.flags&2u) && s.bytes) {
            const Phdr *segment=region(p,s.address,s.bytes,4,0);
            if (!segment || (s.type!=8 &&
                (s.address-segment->address>segment->file_bytes ||
                 s.bytes>segment->file_bytes-(s.address-segment->address) ||
                 s.offset!=segment->offset+s.address-segment->address))) return -1;
        }
        if (s.type==11) {
            if (have_symbols++ || s.entry_bytes!=sizeof(Sym) || s.bytes%sizeof(Sym) ||
                s.bytes>4096u*sizeof(Sym) || !s.bytes || s.link>=e->shnum ||
                !region(p,s.address,s.bytes,4,3)) return -1;
            p->symbols=s;
        }
        if (name_is(source+names.offset,names.bytes,s.name,".dynstr")) {
            if (have_strings++ || s.type!=3 || !s.bytes ||
                !region(p,s.address,s.bytes,4,3)) return -1;
            p->strings=s;
        }
        if (s.type==4 && s.bytes) {
            if (have_relocations++ || s.entry_bytes!=sizeof(Rela) ||
                s.bytes%sizeof(Rela) || s.bytes>65536u*sizeof(Rela) ||
                !region(p,s.address,s.bytes,4,3)) return -1;
            p->relocations=s;
        }
    }
    if (!have_symbols || !have_strings) return -1;

    int terminated=0;
    for (uint64_t at=0; at<dynamic.file_bytes; at+=sizeof(Dyn)) {
        Dyn d;
        copy(&d,source+dynamic.offset+at,sizeof(d));
        if (!d.tag) { terminated=1; break; }
        switch (d.tag) {
        case 1: case 2: case 12: case 13: case 15: case 17: case 18: case 19:
        case 20: case 22: case 23: case 25: case 26: case 27: case 28:
        case 29: case 32: case 33: case 35: case 36: case 37: return -1;
        case 5: dynamic_strings=d.value; break;
        case 6: dynamic_symbols=d.value; break;
        case 7: dynamic_relocations=d.value; break;
        case 8: dynamic_relocation_bytes=d.value; break;
        case 9: if (d.value!=sizeof(Rela)) return -1; break;
        case 10: dynamic_strings_bytes=d.value; break;
        case 11: if (d.value!=sizeof(Sym)) return -1; break;
        default: break;
        }
    }
    if (!terminated || dynamic_symbols!=p->symbols.address ||
        dynamic_strings!=p->strings.address || dynamic_strings_bytes!=p->strings.bytes ||
        dynamic_relocations!=p->relocations.address ||
        dynamic_relocation_bytes!=p->relocations.bytes) return -1;
    Shdr symbol_strings;
    copy(&symbol_strings,source+e->shoff+(uint64_t)p->symbols.link*sizeof(Shdr),sizeof(Shdr));
    if (symbol_strings.offset!=p->strings.offset ||
        symbol_strings.bytes!=p->strings.bytes) return -1;
    for (uint64_t at=0; at<p->symbols.bytes; at+=sizeof(Sym)) {
        Sym symbol;
        copy(&symbol,source+p->symbols.offset+at,sizeof(symbol));
        if (!at) {
            Sym empty={0};
            if (!equal(&symbol,&empty,sizeof(symbol))) return -1;
            continue;
        }
        if (!symbol.section || symbol.section>=e->shnum || (symbol.info&15u)==10u ||
            symbol.name>=p->strings.bytes ||
            !region(p,symbol.value,symbol.bytes,4,0)) return -1;
        if (name_is(source+p->strings.offset,p->strings.bytes,symbol.name,IFS_FS_COMMON_EXPORT_V2)) {
            if (p->entry || (symbol.info&15u)!=2u || !symbol.bytes ||
                !region(p,symbol.value,symbol.bytes,5,2)) return -1;
            p->entry=symbol.value;
        }
    }
    if (!p->entry) return -1;
    uint64_t previous_target=0;
    for (uint64_t at=0; at<p->relocations.bytes; at+=sizeof(Rela)) {
        Rela relocation;
        copy(&relocation,source+p->relocations.offset+at,sizeof(relocation));
        if (relocation.info!=8u || relocation.addend<0 || (relocation.target&7u) ||
            (at && relocation.target<=previous_target) ||
            !region(p,relocation.target,sizeof(uint64_t),6,1) ||
            !region(p,(uint64_t)relocation.addend,1,4,0)) return -1;
        previous_target=relocation.target;
    }
    return 0;
}
int BootSoValidate(const void *data, size_t bytes) {
    Plan p;
    return plan(data,bytes,&p);
}
int BootSoContains(const BootSoImage *image, const void *address, size_t bytes,
                    uint32_t required, uint32_t forbidden) {
    uintptr_t base=(uintptr_t)image->base, ptr=(uintptr_t)address;
    if (!image->base || ptr<base || !span(ptr-base,bytes,image->bytes)) return 0;
    for (uint32_t i=0; i<image->segment_count; ++i) {
        const BootSoSegment *s=&image->segments[i];
        if (ptr-base>=s->address && span(ptr-base-s->address,bytes,s->bytes) &&
            (s->flags&required)==required && !(s->flags&forbidden)) return 1;
    }
    return 0;
}
void BootSoUnload(BootSoImage *image) {
    if (image && image->base) image->memory.release(image->memory.context,image->base,image->bytes);
    if (image) zero(image,sizeof(*image));
}

static const IfsFsGroupHeaderV2 *bounded_group(const BootSoImage *image,
        const IfsFsModuleApiV2 *api,uint32_t id,uint32_t size,uint64_t capabilities) {
    const IfsFsGroupHeaderV2 *group=api->query_group(id,IFS_FS_GROUP_ABI_V2,IFS_FS_GROUP_ABI_V2);
    if(!BootSoContains(image,group,size,BOOT_SO_READ,BOOT_SO_EXECUTE) ||
       !ifs_fs_group_v2_header_valid(group,id,size,capabilities) ||
       !ifs_fs_group_v2_callbacks_valid(group)) return NULL;
    return group;
}

static int bind_common_v2(BootSoImage *image,const IfsFsModuleApiV2 *api) {
    const uint64_t required=IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|IFS_FS_CORE_STAT|
                            IFS_FS_CORE_LIST|IFS_FS_CORE_READ;
    if(!BootSoContains(image,api,sizeof(*api),BOOT_SO_READ,BOOT_SO_EXECUTE) ||
       api->abi_version!=IFS_FS_COMMON_ABI_V2 || api->structure_bytes<sizeof(*api) ||
       !(api->runtime_profiles&(IFS_FS_PROFILE_FREESTANDING|IFS_FS_PROFILE_FIRMWARE)) ||
       (api->runtime_profiles&~IFS_FS_PROFILE_ALL) || (api->group_mask&~IFS_FS_GROUP_ALL) ||
       !(api->group_mask&IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE))) return -1;

    /* Descriptor addresses are relocated ELF pointers. Check their mapped
     * ranges before calling the group query or dereferencing its result. */
    for(size_t i=0;i<sizeof(image->format_id);++i) {
        const uintptr_t id=(uintptr_t)api->format_id;
        if(id>UINTPTR_MAX-i || !BootSoContains(image,(const void *)(id+i),1,
                                                BOOT_SO_READ,BOOT_SO_EXECUTE)) return -1;
        image->format_id[i]=*(const char *)(id+i);
        if(!image->format_id[i]) break;
    }
    if(!ifs_fs_format_id_valid(image->format_id)) return -1;

#define EXECUTABLE(callback) BootSoContains(image,(const void *)(uintptr_t)(callback),1, \
                                           BOOT_SO_READ|BOOT_SO_EXECUTE,BOOT_SO_WRITE)
    if(!EXECUTABLE(api->query_group)) return -1;
    const IfsFsCoreOpsV2 *core=(const IfsFsCoreOpsV2 *)bounded_group(image,api,
                                      IFS_FS_GROUP_CORE,sizeof(*core),IFS_FS_CORE_ALL);
    if(!core || (core->header.capabilities&required)!=required ||
       !EXECUTABLE(core->probe) || !EXECUTABLE(core->mount) || !EXECUTABLE(core->unmount) ||
       !EXECUTABLE(core->stat) || !EXECUTABLE(core->directory_entry) || !EXECUTABLE(core->read)) return -1;
    if(api->group_mask&IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME)) {
        const IfsFsVolumeOpsV2 *volume=(const IfsFsVolumeOpsV2 *)bounded_group(image,api,
                                  IFS_FS_GROUP_VOLUME,sizeof(*volume),IFS_FS_VOLUME_ALL);
        if(!volume || (volume->label && !EXECUTABLE(volume->label))) return -1;
        image->volume=volume;
    }
#undef EXECUTABLE
    image->core=core;
    return 0;
}

int BootSoLoad(const void *data, size_t bytes, const BootSoMemory *memory,
                BootSoImage *image) {
    Plan p;
    if (!image || !memory || !memory->allocate || !memory->release ||
        plan(data,bytes,&p)) return -1;
    zero(image,sizeof(*image));
    image->memory=*memory;
    image->base=memory->allocate(memory->context,(size_t)p.image_bytes);
    if (!image->base) return -1;
    image->bytes=(size_t)p.image_bytes;
    if ((uintptr_t)image->base&4095u) { BootSoUnload(image); return -1; }
    zero(image->base,image->bytes);
    image->segment_count=p.count;
    for (uint32_t i=0; i<p.count; ++i) {
        const Phdr *s=&p.loads[i];
        image->segments[i]=(BootSoSegment){s->address,s->memory_bytes,s->flags};
        copy((unsigned char *)image->base+s->address,
             (const unsigned char *)data+s->offset,(size_t)s->file_bytes);
    }
    for (uint64_t at=0; at<p.relocations.bytes; at+=sizeof(Rela)) {
        Rela r;
        copy(&r,(const unsigned char *)data+p.relocations.offset+at,sizeof(r));
        uint64_t value=(uint64_t)(uintptr_t)image->base+(uint64_t)r.addend;
        copy((unsigned char *)image->base+r.target,&value,sizeof(value));
    }
    if (memory->protect) {
        for (uint32_t i=0; i<p.count; ++i) {
            const Phdr *s=&p.loads[i];
            uint64_t start=s->address&~UINT64_C(4095);
            size_t length=(size_t)(((s->address+s->memory_bytes+4095u)&~UINT64_C(4095))-start);
            if (memory->protect(memory->context,(unsigned char *)image->base+start,
                                length,s->flags)) { BootSoUnload(image); return -1; }
        }
    }
    IfsFsModuleApiEntryV2 entry;
    void *address=(unsigned char *)image->base+p.entry;
    _Static_assert(sizeof(entry)==sizeof(address),"function pointer representation");
    copy(&entry,&address,sizeof(entry));
    if(bind_common_v2(image,entry())==0) return 0;
    BootSoUnload(image);
    return -1;
}
