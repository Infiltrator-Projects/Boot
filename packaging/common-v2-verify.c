/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Installer-side extraction and structural validation. Never call a module's
 * entry point on Linux, and never load a hosted ELF with dlopen(). Firmware
 * repeats authentication and validates the canonical descriptor at boot. */
#define _GNU_SOURCE
#include "../boot/common_v2_elf.h"
#include "../boot/common_v2_trust.h"
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define MAX_FILE (128U*1024U*1024U)
#define MAX_DB (4U*1024U*1024U+4U)
typedef struct {unsigned char *p;size_t n;} Buffer;
typedef struct {Buffer db,dbx;} Trust;
static int span(uint64_t o,uint64_t n,uint64_t total)
{return o<=total && n<=total-o;}
static int load(const char *path,size_t limit,Buffer *b)
{
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    struct stat st;size_t cap=4096;
    if(fd<0)return -1;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||
       (uint64_t)st.st_size>limit)goto bad;
    if(cap>limit+1)cap=limit+1;
    b->p=malloc(cap);if(!b->p)goto bad;
    for(;;){
        if(b->n==cap){
            unsigned char *p;size_t next=cap>limit/2?limit+1:cap*2;
            if(cap==limit+1)goto bad;
            p=realloc(b->p,next);if(!p)goto bad;b->p=p;cap=next;
        }
        ssize_t r=read(fd,b->p+b->n,cap-b->n);
        if(r<0&&errno==EINTR)continue;
        if(r<0)goto bad;
        if(!r)break;
        b->n+=(size_t)r;if(b->n>limit)goto bad;
    }
    close(fd);if(b->n)return 0;
    free(b->p);*b=(Buffer){0};return -1;
bad:
    close(fd);free(b->p);*b=(Buffer){0};return -1;
}
static int variable(const char *dir,const char *name,size_t limit,Buffer *b)
{
    char path[4096];int n=snprintf(path,sizeof(path),"%s/%s",dir,name);
    return n<0||(size_t)n>=sizeof(path)?-1:load(path,limit,b);
}
static int user_mode(const char *dir)
{
    Buffer b={0};int ok=!variable(dir,"SetupMode-8be4df61-93ca-11d2-aa0d-00e098032b8c",5,&b)&&b.n==5&&b.p[4]==0;
    free(b.p);return ok;
}
static int db_ok(const Buffer *b)
{
    uint32_t a=0;if(b->n<=4)return 0;
    memcpy(&a,b->p,4);return (a&0x27)==0x27;
}
static int authenticate(void *c,const void *p,size_t n)
{
    Trust *t=c;uint8_t digest[32];boot_v2_sha256(p,n,digest);
    return boot_v2_trust_digest(t->db.p+4,t->db.n-4,t->dbx.p+4,t->dbx.n-4,digest);
}
static void *reserve(void *c,size_t n)
{(void)c;void *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);return p==MAP_FAILED?NULL:p;}
static int protect(void *c,void *p,size_t n,unsigned flags)
{
    (void)c;
    /* Structural validation needs no executable memory on the install host. */
    return mprotect(p,n,((flags&BOOT_V2_ELF_R)?PROT_READ:0)|((flags&BOOT_V2_ELF_W)?PROT_WRITE:0));
}
static void release(void *c,void *p,size_t n){(void)c;(void)munmap(p,n);}
static Elf64_Shdr section(const Buffer *b,const Elf64_Ehdr *e,unsigned i)
{Elf64_Shdr s;memcpy(&s,b->p+e->e_shoff+i*sizeof(s),sizeof(s));return s;}
static int payload(const Buffer *b,const unsigned char **out,size_t *n)
{
    Elf64_Ehdr e;Elf64_Shdr names;unsigned found=0;
    if(b->n<sizeof(e))return -1;
    memcpy(&e,b->p,sizeof(e));
    if(memcmp(e.e_ident,ELFMAG,SELFMAG)||e.e_ident[EI_CLASS]!=ELFCLASS64||
       e.e_ident[EI_DATA]!=ELFDATA2LSB||e.e_ident[EI_VERSION]!=EV_CURRENT||
       e.e_version!=EV_CURRENT||e.e_type!=ET_DYN||e.e_machine!=EM_X86_64||
       e.e_ehsize!=sizeof(e)||e.e_shentsize!=sizeof(Elf64_Shdr)||
       !e.e_shnum||e.e_shnum>4096||e.e_shstrndx>=e.e_shnum||
       !span(e.e_shoff,(uint64_t)e.e_shnum*sizeof(Elf64_Shdr),b->n))return -1;
    names=section(b,&e,e.e_shstrndx);
    if(names.sh_type!=SHT_STRTAB||!span(names.sh_offset,names.sh_size,b->n))return -1;
    *out=b->p;*n=b->n;
    for(unsigned i=0;i<e.e_shnum;i++){
        Elf64_Shdr s=section(b,&e,i);
        if(s.sh_name>=names.sh_size)return -1;
        const char *name=(const char *)b->p+names.sh_offset+s.sh_name;
        if(!memchr(name,0,names.sh_size-s.sh_name))return -1;
        if(strcmp(name,".infiltrator_boot"))continue;
        if(++found!=1||s.sh_type!=SHT_PROGBITS||!s.sh_size||
           !span(s.sh_offset,s.sh_size,b->n))return -1;
        *out=b->p+s.sh_offset;*n=(size_t)s.sh_size;
    }
    return 0;
}
int main(int argc,char **argv)
{
    Buffer input={0};Trust t={0};BootV2ElfImage image={0};
    BootV2ElfHost h={&t,authenticate,reserve,protect,release};
    const unsigned char *p=NULL;size_t n=0,written=0;int fd=-1,rc=1,created=0;
    if(argc!=4){fprintf(stderr,"Usage: %s INPUT OUTPUT EFIVARS_DIRECTORY\n",argv[0]);return 2;}
    if(!user_mode(argv[3])||
       variable(argv[3],"db-d719b2cb-3d3a-4596-a3bc-dad00e67656f",MAX_DB,&t.db)||
       variable(argv[3],"dbx-d719b2cb-3d3a-4596-a3bc-dad00e67656f",MAX_DB,&t.dbx)||
       !db_ok(&t.db)||!db_ok(&t.dbx)||load(argv[1],MAX_FILE,&input)||
       payload(&input,&p,&n)||boot_v2_elf_load(&h,p,n,&image)||!user_mode(argv[3]))goto out;
    fd=open(argv[2],O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)goto out;
    created=1;
    while(written<n){
        ssize_t r=write(fd,p+written,n-written);
        if(r<0&&errno==EINTR)continue;
        if(r<=0)goto out;
        written+=(size_t)r;
    }
    if(fsync(fd))goto out;
    if(close(fd)){fd=-1;goto out;}
    fd=-1;rc=0;
out:
    if(fd>=0)close(fd);
    if(rc&&created)unlink(argv[2]);
    boot_v2_elf_unload(&h,&image);
    free(input.p);free(t.db.p);free(t.dbx.p);
    if(rc)fprintf(stderr,"BOOT: firmware ELF extraction, trust or structural validation failed: %s\n",argv[1]);
    return rc;
}
