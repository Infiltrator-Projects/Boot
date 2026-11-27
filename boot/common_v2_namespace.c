/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_namespace.h"

/* Paths and namespace resolution share one UTF-8 decoder and one root-
 * confined parent operation. No filesystem metadata is interpreted here. */
static void parent(char *p,size_t *n)
{
    while(*n>1&&p[*n-1]!='/')--*n;
    if(*n>1)--*n;
    p[*n]=0;
}

int boot_v2_utf8_next(const char *text,size_t bytes,size_t *cursor,
                                    uint32_t *scalar)
{
    size_t i=*cursor;
    uint32_t c,min=0;
    unsigned extra=0;

    if(i>=bytes)return -1;
    c=(unsigned char)text[i++];
    if(!c)return -1;
    if(c<0x80){}
    else if(c>=0xc2&&c<=0xdf){c&=31;extra=1;min=0x80;}
    else if(c>=0xe0&&c<=0xef){c&=15;extra=2;min=0x800;}
    else if(c>=0xf0&&c<=0xf4){c&=7;extra=3;min=0x10000;}
    else return -1;
    if(extra>bytes-i)return -1;
    while(extra--){
        unsigned x=(unsigned char)text[i++];
        if((x&0xc0)!=0x80)return -1;
        c=(c<<6)|(x&63);
    }
    if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return -1;
    *cursor=i;
    *scalar=c;
    return 0;
}

static int sep(uint16_t c){return c=='/' || c=='\\';}
static int add(char *out,size_t cap,size_t *at,uint32_t c)
{
    unsigned n=c<0x80?1:c<0x800?2:c<0x10000?3:4;
    if(*at>=cap || n>=cap-*at) return -1;
    if(n==1) out[(*at)++]=(char)c;
    else {
        out[(*at)++]=(char)((n==2?0xc0:n==3?0xe0:0xf0)|(c>>(6*(n-1))));
        while(--n) out[(*at)++]=(char)(0x80|((c>>(6*(n-1)))&63));
    }
    out[*at]=0;return 0;
}
int boot_v2_path(const char *base,const uint16_t *name,char *out,size_t cap)
{
    size_t at=0,i=0,components=0;
    if(!base || *base!='/' || !name || !out || cap<2 || cap>BOOT_V2_PATH_BYTES) return -1;
    /* Absolute paths discard the parent; relative paths start at the
     * existing normalized directory. Neither can escape volume root. */
    if(sep(name[0])) {out[0]='/';out[1]=0;at=1;}
    else {
        while(base[at]){
            if(at+1>=cap)return -1;
            out[at]=base[at];++at;
        }
        out[at]=0;
    }
    while(name[i]){
        size_t start,mark;
        while(sep(name[i])) if(++i>=BOOT_V2_PATH_BYTES)return -1;
        if(!name[i])break;
        if(++components>BOOT_V2_PATH_BYTES/2)return -1;
        start=i;
        while(name[i]&&!sep(name[i])) if(++i>=BOOT_V2_PATH_BYTES)return -1;
        if(i-start==1 && name[start]=='.')continue;
        if(i-start==2 && name[start]=='.'&&name[start+1]=='.'){
            parent(out,&at);continue;
        }
        if(at>1 && add(out,cap,&at,'/'))return -1;
        mark=start;
        while(mark<i){
            uint32_t c=name[mark++];
            if(c>=0xd800 && c<=0xdbff){
                uint32_t low;
                if(mark>=i)return -1;
                low=name[mark++];
                if(low<0xdc00||low>0xdfff)return -1;
                c=0x10000+((c-0xd800)<<10)+(low-0xdc00);
            }else if(c>=0xdc00 && c<=0xdfff)return -1;
            if(add(out,cap,&at,c))return -1;
        }
    }
    return 0;
}
int boot_v2_utf16(const char *name,size_t bytes,uint16_t *out,size_t cap,size_t *units)
{
    size_t i=0,n=0;
    if(!name || !out || !cap || !units)return -1;
    while(i<bytes){
        uint32_t c;
        if(boot_v2_utf8_next(name,bytes,&i,&c))return -1;
        if(c>=0x10000){
            if(n>=cap||cap-n<3)return -1;
            c-=0x10000;out[n++]=(uint16_t)(0xd800|(c>>10));out[n++]=(uint16_t)(0xdc00|(c&1023));
        }else{
            if(n>=cap||cap-n<2)return -1;
            out[n++]=(uint16_t)c;
        }
    }
    out[n]=0;*units=n;return 0;
}

#define LINK_LIMIT 32u
static size_t length(const char *p,size_t cap)
{size_t n=0;while(n<cap&&p[n])++n;return n;}
static void copy(char *d,const char *s,size_t n)
{for(size_t i=0;i<n;i++)d[i]=s[i];}

static int valid_name(const IfsFsEntryV2 *e,const char *p)
{
    return e->structure_bytes==sizeof(*e)&&e->name==p&&
           e->name_capacity==BOOT_V2_PATH_BYTES&&e->name_bytes<BOOT_V2_PATH_BYTES&&
           length(p,BOOT_V2_PATH_BYTES)==e->name_bytes&&
           (e->type==IFS_FS_ENTRY_FILE||e->type==IFS_FS_ENTRY_DIRECTORY||e->type==IFS_FS_ENTRY_LINK);
}
int boot_v2_stat_entry(const IfsFsCoreOpsV2 *core,void *v,const char *path,
                       IfsFsEntryV2 *e,char name[BOOT_V2_PATH_BYTES])
{
    if(!core||!core->stat||!path||!e||!name)return IFS_FS_INVALID;
    /* Fill the name with nonzero bytes: missing terminators cannot pass
     * by coincidence using stale stack contents or zero-initialization. */
    for(size_t i=0;i<BOOT_V2_PATH_BYTES;i++)name[i]=(char)0xff;
    *e=(IfsFsEntryV2){.structure_bytes=sizeof(*e),.name=name,.name_capacity=BOOT_V2_PATH_BYTES};
    int rc=core->stat(v,path,e);
    return rc==IFS_FS_OK&&!valid_name(e,name)?IFS_FS_CORRUPT:rc;
}
static int link_target_valid(const char *target,size_t n)
{
    /* UTF-8 scalar validation; slash alone separates components. EFI
     * cannot represent a literal Unix backslash within one filename. */
    size_t i=0;
    if(!n||n>=BOOT_V2_PATH_BYTES)return 0;
    while(i<n){
        uint32_t c;
        if(boot_v2_utf8_next(target,n,&i,&c)||c=='\\')return 0;
    }
    return 1;
}
int boot_v2_resolve(const IfsFsCoreOpsV2 *core,const IfsFsMetadataOpsV2 *meta,
                   void *v,const char *path,char out[BOOT_V2_PATH_BYTES],
                   IfsFsEntryV2 *entry,char name[BOOT_V2_PATH_BYTES])
{
    char pending[BOOT_V2_PATH_BYTES],target[BOOT_V2_PATH_BYTES];
    size_t n,at=0,used=1;unsigned links=0;
    int rc;
    if(!path||*path!='/'||!out||!entry||!name)return IFS_FS_INVALID;
    n=length(path,BOOT_V2_PATH_BYTES);
    if(n==BOOT_V2_PATH_BYTES)return IFS_FS_INVALID;
    rc=boot_v2_stat_entry(core,v,path,entry,name);
    if(rc==IFS_FS_OK&&entry->type!=IFS_FS_ENTRY_LINK){copy(out,path,n+1);return rc;}
    if(!meta||!meta->readlink||!(meta->header.capabilities&IFS_FS_META_READLINK))
        return rc==IFS_FS_OK?IFS_FS_UNSUPPORTED:rc;
    if(rc!=IFS_FS_OK&&rc!=IFS_FS_NOT_FOUND&&rc!=IFS_FS_UNSUPPORTED&&rc!=IFS_FS_INVALID)
        return rc;
    /* Restart at root; resolve each component before interpreting '..'.
     * Reinsert a link target ahead of the unconsumed suffix, preserving the
     * containing directory for relative links. No host path operations. */
    copy(pending,path,n+1);out[0]='/';out[1]=0;
    while(at<n){
        size_t start,end,old=used;
        while(at<n&&pending[at]=='/')++at;
        if(at==n)break;
        start=at;
        while(at<n&&pending[at]!='/')++at;
        end=at;
        if(end-start==1&&pending[start]=='.')continue;
        if(end-start==2&&pending[start]=='.'&&pending[start+1]=='.'){
            parent(out,&used);continue;
        }
        if(used>1){
            if(used+1>=BOOT_V2_PATH_BYTES)return IFS_FS_INVALID;
            out[used++]='/';
        }
        if(end-start>=BOOT_V2_PATH_BYTES-used)return IFS_FS_INVALID;
        copy(out+used,pending+start,end-start);used+=end-start;out[used]=0;
        rc=boot_v2_stat_entry(core,v,out,entry,name);
        if(rc!=IFS_FS_OK)return rc;
        if(entry->type==IFS_FS_ENTRY_LINK){
            uint32_t bytes=0;size_t rest=n-at;
            if(++links>LINK_LIMIT)return IFS_FS_CORRUPT;
            rc=meta->readlink(v,out,target,sizeof(target),&bytes);
            if(rc!=IFS_FS_OK)return rc;
            if(!link_target_valid(target,bytes)||bytes>=BOOT_V2_PATH_BYTES-rest)
                return IFS_FS_CORRUPT;
            /* target and pending are distinct; copying the suffix first
             * avoids overlap when the replacement changes its length. */
            copy(target+bytes,pending+at,rest);
            n=bytes+rest;target[n]=0;
            if(target[0]=='/')used=1;else used=old;
            out[used]=0;copy(pending,target,n+1);at=0;
            continue;
        }
        if(at<n&&entry->type!=IFS_FS_ENTRY_DIRECTORY)return IFS_FS_NOT_FOUND;
    }
    rc=boot_v2_stat_entry(core,v,out,entry,name);
    if(rc==IFS_FS_OK&&entry->type==IFS_FS_ENTRY_LINK)return IFS_FS_CORRUPT;
    return rc;
}
