/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_namespace.h"
#define LINK_LIMIT 32u
static size_t length(const char *p,size_t cap)
{size_t n=0;while(n<cap&&p[n])++n;return n;}
static void copy(char *d,const char *s,size_t n)
{for(size_t i=0;i<n;i++)d[i]=s[i];}
static void parent(char *p,size_t *n)
{
    while(*n>1&&p[*n-1]!='/')--*n;
    if(*n>1)--*n;
    p[*n]=0;
}
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
        uint32_t c=(unsigned char)target[i++],min=0;unsigned extra=0;
        if(!c||c=='\\')return 0;
        if(c<0x80)continue;
        if(c>=0xc2&&c<=0xdf){c&=31;extra=1;min=0x80;}
        else if(c>=0xe0&&c<=0xef){c&=15;extra=2;min=0x800;}
        else if(c>=0xf0&&c<=0xf4){c&=7;extra=3;min=0x10000;}
        else return 0;
        if(extra>n-i)return 0;
        while(extra--){unsigned x=(unsigned char)target[i++];if((x&0xc0)!=0x80)return 0;c=(c<<6)|(x&63);}
        if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return 0;
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
