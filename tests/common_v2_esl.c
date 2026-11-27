/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Produce a standard raw SHA-256 EFI_SIGNATURE_LIST for disposable
 * qualification or offline platform-owner authorization. No NVRAM writes. */
#include "../boot/common_v2_trust.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
int main(int argc,char **argv)
{
    const unsigned char type[16]={0x26,0x16,0xc4,0xc1,0x4c,0x50,0x92,0x40,0xac,0xa9,0x41,0xf9,0x36,0x93,0x43,0x28};
    unsigned char list[76]={0};
    FILE *f;long n;unsigned char *p;
    if(argc!=2||!(f=fopen(argv[1],"rb")))return 1;
    if(fseek(f,0,SEEK_END)||(n=ftell(f))<=0||n>64L*1024*1024){fclose(f);return 1;}
    rewind(f);p=malloc((size_t)n);
    if(!p||fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return 1;}
    fclose(f);
    for(unsigned i=0;i<16;i++)list[i]=type[i];
    list[16]=sizeof(list);list[24]=48; /* EFI_SIGNATURE_LIST prefix */
    /* Fixture owner GUID is intentionally independent of the digest. */
    list[28]=0xb1;list[29]=0xc2;list[30]=0x22;list[31]=0xd8;
    boot_v2_sha256(p,(size_t)n,list+44);free(p);
    return fwrite(list,1,sizeof(list),stdout)==sizeof(list)?0:1;
}
