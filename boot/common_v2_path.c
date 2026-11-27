/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_path.h"

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
            while(at>1 && out[at-1]!='/')--at;
            if(at>1)--at;
            out[at]=0;continue;
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
