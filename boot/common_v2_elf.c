/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_elf.h"
#include <elf.h>

/* Bound memory consumption and complexity before allocating firmware pages.
 * This initial profile supports position-independent, dependency-free ELF
 * with RELA relative relocations. TLS, constructors, imported symbols and
 * unsupported dynamic features fail closed rather than being emulated. */
#define PAGE UINT64_C(4096)
#define LIMIT (UINT64_C(64) * 1024 * 1024)
#define ENTRY "infiltrator_filesystem_api_v2"
#define ROUND(x) (((x) + PAGE - 1) & ~(PAGE - 1))

static int span(uint64_t off, uint64_t len, uint64_t total)
{ return off <= total && len <= total - off; }
static void copy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst; const unsigned char *s = src;
    while (n--) *d++ = *s++;
}
static void zero(void *dst, size_t n)
{ unsigned char *d = dst; while (n--) *d++ = 0; }
static int nameeq(const char *p, size_t n, const char *s)
{
    while (n && *s && *p == *s) { ++p; ++s; --n; }
    return n && !*p && !*s;
}
static Elf64_Phdr ph(const unsigned char *f, const Elf64_Ehdr *e, unsigned i)
{ Elf64_Phdr p; copy(&p, f + e->e_phoff + i * sizeof(p), sizeof(p)); return p; }
static Elf64_Shdr sh(const unsigned char *f, const Elf64_Ehdr *e, unsigned i)
{ Elf64_Shdr s; copy(&s, f + e->e_shoff + i * sizeof(s), sizeof(s)); return s; }

/* A virtual range must lie wholly within one real PT_LOAD, not a hole or
 * page padding. For file-backed data, verify the exact file correspondence. */
static int region(const unsigned char *f, const Elf64_Ehdr *e, uint64_t va,
                  uint64_t n, unsigned flags, int backed, uint64_t off)
{
    unsigned i;
    for (i=0; i<e->e_phnum; ++i) {
        Elf64_Phdr p=ph(f,e,i);
        if (p.p_type != PT_LOAD || (p.p_flags & flags) != flags || va < p.p_vaddr)
            continue;
        if (span(va-p.p_vaddr,n,backed?p.p_filesz:p.p_memsz) &&
            (!backed || off == p.p_offset+va-p.p_vaddr)) return 1;
    }
    return 0;
}

int boot_v2_elf_contains(const BootV2ElfImage *image, uintptr_t ptr, size_t bytes,
                         unsigned required, unsigned forbidden)
{
    uintptr_t base;
    size_t offset;
    unsigned i;
    if (!image || !image->base || !image->entry || !bytes ||
        !image->region_count || image->region_count>BOOT_V2_ELF_MAX_SEGMENTS ||
        (required|forbidden)&~7u || required&forbidden) return 0;
    base=(uintptr_t)image->base;
    if (ptr<base || !span(ptr-base,bytes,image->bytes)) return 0;
    offset=ptr-base;
    for(i=0;i<image->region_count;++i) {
        const BootV2ElfRegion *r=&image->regions[i];
        unsigned flags=r->flags;
        if(offset<r->offset || !span(offset-r->offset,bytes,r->bytes)) continue;
        if((required&BOOT_V2_ELF_X) && !span(offset-r->offset,bytes,r->file_bytes)) return 0;
        if(image->relro_end>image->relro_start && offset<image->relro_end &&
           image->relro_start<offset+bytes) {
            /* Reject spans across the permission boundary; callers can
             * query individual ranges without guessing their access. */
            if(offset<image->relro_start || offset+bytes>image->relro_end) return 0;
            flags=BOOT_V2_ELF_R;
        }
        return (flags&required)==required && !(flags&forbidden);
    }
    return 0;
}

void boot_v2_elf_unload(const BootV2ElfHost *h, BootV2ElfImage *out)
{
    if (!out) return;
    if (out->base && h && h->release) h->release(h->context,out->base,out->bytes);
    zero(out,sizeof(*out));
}

int boot_v2_elf_load(const BootV2ElfHost *h, const void *data, size_t n,
                     BootV2ElfImage *out)
{
    const unsigned char *f=data;
    Elf64_Ehdr e; Elf64_Phdr dyn={0}, relro={0};
    Elf64_Shdr sym={0}, str={0};
    uint64_t end=0, rela=0, relasz=0, relaent=0, symtab=0, strtab=0, strsz=0, syment=0;
    uint64_t entry=0, i,j, seen=0; unsigned loads=0, found=0;
    int terminated=0;
    unsigned char *base;
    if (!out || out->base || out->entry || out->bytes || out->region_count) return -1;
    if (!h || !h->authenticate || !h->reserve || !h->protect || !h->release ||
        !f || n<sizeof(e) || n>LIMIT || h->authenticate(h->context,f,n)!=0) return -1;
    copy(&e,f,sizeof(e));
    if (e.e_ident[0]!=ELFMAG0 || e.e_ident[1]!=ELFMAG1 ||
        e.e_ident[2]!=ELFMAG2 || e.e_ident[3]!=ELFMAG3 ||
        e.e_ident[EI_CLASS]!=ELFCLASS64 || e.e_ident[EI_DATA]!=ELFDATA2LSB ||
        e.e_ident[EI_VERSION]!=EV_CURRENT || e.e_version!=EV_CURRENT ||
        e.e_type!=ET_DYN || e.e_machine!=EM_X86_64 || e.e_ehsize!=sizeof(e) ||
        e.e_phentsize!=sizeof(Elf64_Phdr) || !e.e_phnum || e.e_phnum>BOOT_V2_ELF_MAX_SEGMENTS ||
        e.e_shentsize!=sizeof(Elf64_Shdr) || !e.e_shnum || e.e_shnum>4096 ||
        !span(e.e_phoff,(uint64_t)e.e_phnum*sizeof(Elf64_Phdr),n) ||
        !span(e.e_shoff,(uint64_t)e.e_shnum*sizeof(Elf64_Shdr),n)) return -1;
    for (i=0;i<e.e_phnum;++i) {
        Elf64_Phdr p=ph(f,&e,i);
        if (!span(p.p_offset,p.p_filesz,n)) return -1;
        switch (p.p_type) {
        case PT_LOAD:
            if (!p.p_memsz || p.p_filesz>p.p_memsz || !span(p.p_vaddr,p.p_memsz,LIMIT) ||
                p.p_align!=PAGE || (p.p_vaddr%PAGE)!=(p.p_offset%PAGE) ||
                (p.p_flags & ~(PF_R|PF_W|PF_X)) || !(p.p_flags&PF_R) ||
                (p.p_flags&(PF_W|PF_X))==(PF_W|PF_X)) return -1;
            for(j=0;j<i;++j) {
                Elf64_Phdr q=ph(f,&e,j);
                if(q.p_type==PT_LOAD && (p.p_vaddr&~(PAGE-1))<ROUND(q.p_vaddr+q.p_memsz) &&
                   (q.p_vaddr&~(PAGE-1))<ROUND(p.p_vaddr+p.p_memsz)) return -1;
            }
            if(ROUND(p.p_vaddr+p.p_memsz)>end) end=ROUND(p.p_vaddr+p.p_memsz);
            ++loads; break;
        case PT_DYNAMIC: if(dyn.p_type) return -1; dyn=p; break;
        case PT_GNU_RELRO: if(relro.p_type) return -1; relro=p; break;
        case PT_GNU_STACK: if(p.p_flags&PF_X) return -1; break;
        case PT_NULL: case PT_PHDR: case PT_NOTE: case PT_GNU_EH_FRAME: break;
        default: return -1;
        }
    }
    if(!loads || !dyn.p_filesz || dyn.p_filesz%sizeof(Elf64_Dyn) ||
       !region(f,&e,dyn.p_vaddr,dyn.p_filesz,PF_R,1,dyn.p_offset)) return -1;
    if(relro.p_type && (!relro.p_memsz || !span(relro.p_vaddr,relro.p_memsz,end) ||
       (relro.p_vaddr+relro.p_memsz)%PAGE ||
       !region(f,&e,relro.p_vaddr,relro.p_memsz,PF_W,0,0))) return -1;
    /* Dynamic tags are an allowlist, not a best-effort Linux linker. */
    for(i=0;i<dyn.p_filesz;i+=sizeof(Elf64_Dyn)) {
        Elf64_Dyn d; unsigned bit=0; uint64_t *slot=0;
        copy(&d,f+dyn.p_offset+i,sizeof(d));
        if(d.d_tag==DT_NULL) { terminated=1; break; }
        switch(d.d_tag) {
        case DT_RELA: bit=1; slot=&rela; break;
        case DT_RELASZ: bit=2; slot=&relasz; break;
        case DT_RELAENT: bit=3; slot=&relaent; break;
        case DT_SYMTAB: bit=4; slot=&symtab; break;
        case DT_STRTAB: bit=5; slot=&strtab; break;
        case DT_STRSZ: bit=6; slot=&strsz; break;
        case DT_SYMENT: bit=7; slot=&syment; break;
        case DT_HASH: case DT_GNU_HASH: case DT_RELACOUNT: case DT_SYMBOLIC:
        case DT_BIND_NOW: break;
        case DT_FLAGS: if(d.d_un.d_val & ~(uint64_t)(DF_SYMBOLIC|DF_BIND_NOW)) return -1; break;
        case DT_FLAGS_1: if(d.d_un.d_val & ~(uint64_t)DF_1_NOW) return -1; break;
        default: return -1;
        }
        if(slot) { if(seen&(UINT64_C(1)<<bit)) return -1; seen|=UINT64_C(1)<<bit; *slot=d.d_un.d_val; }
    }
    if(!terminated || (seen&0xf0)!=0xf0 || !strsz || syment!=sizeof(Elf64_Sym) ||
       ((seen&14)!=0 && ((seen&14)!=14 || relaent!=sizeof(Elf64_Rela) ||
        relasz%sizeof(Elf64_Rela) || !region(f,&e,rela,relasz,PF_R,0,0)))) return -1;
    for(i=0;i<e.e_shnum;++i) {
        Elf64_Shdr s=sh(f,&e,i);
        if(s.sh_type!=SHT_NOBITS && !span(s.sh_offset,s.sh_size,n)) return -1;
        if(s.sh_type==SHT_DYNSYM) {
            if(sym.sh_type || s.sh_link>=e.e_shnum || s.sh_entsize!=sizeof(Elf64_Sym) ||
               !s.sh_size || s.sh_size%sizeof(Elf64_Sym)) return -1;
            sym=s; str=sh(f,&e,s.sh_link);
        }
    }
    if(!sym.sh_type || str.sh_type!=SHT_STRTAB || sym.sh_addr!=symtab || str.sh_addr!=strtab ||
       str.sh_size!=strsz || !region(f,&e,symtab,sym.sh_size,PF_R,1,sym.sh_offset) ||
       !region(f,&e,strtab,strsz,PF_R,1,str.sh_offset)) return -1;
    for(i=0;i<sym.sh_size;i+=sizeof(Elf64_Sym)) {
        Elf64_Sym s; copy(&s,f+sym.sh_offset+i,sizeof(s));
        if(s.st_name>=strsz || (i && s.st_shndx==SHN_UNDEF) || ELF64_ST_TYPE(s.st_info)==STT_GNU_IFUNC) return -1;
        if(!nameeq((const char *)f+str.sh_offset+s.st_name,strsz-s.st_name,ENTRY)) {
            /* The firmware profile has one public symbol, not a mixed ABI
             * or another callable entry point. Local symbols are harmless. */
            if(i && ELF64_ST_BIND(s.st_info)!=STB_LOCAL &&
               (ELF64_ST_VISIBILITY(s.st_other)==STV_DEFAULT ||
                ELF64_ST_VISIBILITY(s.st_other)==STV_PROTECTED)) return -1;
            continue;
        }
        if(++found!=1 || ELF64_ST_TYPE(s.st_info)!=STT_FUNC || ELF64_ST_BIND(s.st_info)!=STB_GLOBAL ||
           ELF64_ST_VISIBILITY(s.st_other)!=STV_DEFAULT || !s.st_shndx || s.st_shndx>=e.e_shnum ||
           !s.st_size || !region(f,&e,s.st_value,s.st_size,PF_X,0,0)) return -1;
        {
            Elf64_Shdr code=sh(f,&e,s.st_shndx);
            if(code.sh_type!=SHT_PROGBITS ||
               (code.sh_flags&(SHF_ALLOC|SHF_EXECINSTR))!=(SHF_ALLOC|SHF_EXECINSTR) ||
               s.st_value<code.sh_addr || !span(s.st_value-code.sh_addr,s.st_size,code.sh_size) ||
               !span(code.sh_offset,code.sh_size,n) ||
               !region(f,&e,s.st_value,s.st_size,PF_X,1,code.sh_offset+s.st_value-code.sh_addr)) return -1;
        }
        entry=s.st_value;
    }
    if(found!=1) return -1;
    base=h->reserve(h->context,end);
    if(!base) return -1;
    out->base=base; out->bytes=end;
    if((uintptr_t)base%PAGE || (uintptr_t)base>UINTPTR_MAX-end) goto fail;
    zero(base,end);
    for(i=0;i<e.e_phnum;++i) {
        Elf64_Phdr p=ph(f,&e,i);
        if(p.p_type==PT_LOAD) copy(base+p.p_vaddr,f+p.p_offset,p.p_filesz);
    }
    /* Relocations may never target the relocation table or read-only code.
     * Read records from the immutable source, not memory being relocated. */
    if(relasz) {
        uint64_t off=0; int located=0;
        for(i=0;i<e.e_phnum;++i) { Elf64_Phdr p=ph(f,&e,i);
            if(p.p_type==PT_LOAD && rela>=p.p_vaddr && span(rela-p.p_vaddr,relasz,p.p_filesz)) {
                off=p.p_offset+rela-p.p_vaddr; located=1; break;
            }
        }
        if(!located) goto fail;
        for(i=0;i<relasz;i+=sizeof(Elf64_Rela)) {
            Elf64_Rela r; uint64_t value; copy(&r,f+off+i,sizeof(r));
            if(ELF64_R_TYPE(r.r_info)!=R_X86_64_RELATIVE || ELF64_R_SYM(r.r_info) ||
               r.r_offset%sizeof(value) || !region(f,&e,r.r_offset,sizeof(value),PF_W,0,0) ||
               r.r_addend<0 || !region(f,&e,(uint64_t)r.r_addend,1,PF_R,0,0)) goto fail;
            value=(uintptr_t)base+(uint64_t)r.r_addend; copy(base+r.r_offset,&value,sizeof(value));
        }
    }
    /* First remove access to the whole reservation, then grant exactly the
     * segment permissions. Gaps stay inaccessible. RELRO is applied last. */
    if(h->protect(h->context,base,end,0)!=0) goto fail;
    for(i=0;i<e.e_phnum;++i) {
        Elf64_Phdr p=ph(f,&e,i); uint64_t start=p.p_vaddr&~(PAGE-1);
        if(p.p_type==PT_LOAD) {
            BootV2ElfRegion *r;
            if(h->protect(h->context,base+start,ROUND(p.p_vaddr+p.p_memsz)-start,p.p_flags)!=0) goto fail;
            r=&out->regions[out->region_count++];
            r->offset=p.p_vaddr; r->bytes=p.p_memsz; r->file_bytes=p.p_filesz; r->flags=p.p_flags;
        }
    }
    if(relro.p_type) {
        uint64_t start=relro.p_vaddr&~(PAGE-1);
        if(h->protect(h->context,base+start,ROUND(relro.p_vaddr+relro.p_memsz)-start,PF_R)!=0) goto fail;
        out->relro_start=start; out->relro_end=ROUND(relro.p_vaddr+relro.p_memsz);
    }
    out->entry=base+entry;
    return 0;
fail:
    boot_v2_elf_unload(h,out);
    return -1;
}
