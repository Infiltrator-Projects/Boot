/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../boot/common_v2_trust.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void hex(const char *s,uint8_t *out)
{
    for(unsigned i=0;i<32;i++){
        unsigned v;assert(sscanf(s+2*i,"%2x",&v)==1);out[i]=v;
    }
}
static void check(const void *p,size_t n,const char *expected)
{
    uint8_t d[32],e[32];hex(expected,e);boot_v2_sha256(p,n,d);assert(!memcmp(d,e,32));
}
static void le(uint8_t *p,uint32_t n)
{for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void list(uint8_t *p,const uint8_t *d)
{
    const uint8_t guid[16]={0x26,0x16,0xc4,0xc1,0x4c,0x50,0x92,0x40,0xac,0xa9,0x41,0xf9,0x36,0x93,0x43,0x28};
    memset(p,0,76);memcpy(p,guid,16);le(p+16,76);le(p+24,48);memcpy(p+44,d,32);
}
int main(void)
{
    uint8_t db[152],dbx[76],digest[32],other[32]={0},million[1000000];
    check("",0,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check("abc",3,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *longer="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check(longer,strlen(longer),"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    memset(million,'a',sizeof(million));
    check(million,sizeof(million),"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    boot_v2_sha256("abc",3,digest);list(db,other);list(db+76,digest);list(dbx,other);
    assert(!boot_v2_trust_digest(db,sizeof(db),dbx,sizeof(dbx),digest));
    assert(boot_v2_trust_digest(db,76,dbx,sizeof(dbx),digest));
    list(dbx,digest);assert(boot_v2_trust_digest(db,sizeof(db),dbx,sizeof(dbx),digest));
    list(dbx,other);assert(boot_v2_trust_digest(db,sizeof(db)-1,dbx,sizeof(dbx),digest));
    assert(boot_v2_trust_digest(db,sizeof(db),dbx,sizeof(dbx)-1,digest));
    le(db+16,0xffffffff);assert(boot_v2_trust_digest(db,sizeof(db),dbx,sizeof(dbx),digest));
    list(db,other);le(dbx+24,0);assert(boot_v2_trust_digest(db,sizeof(db),dbx,sizeof(dbx),digest));
    list(dbx,other);db[76]^=1;assert(boot_v2_trust_digest(db,sizeof(db),dbx,sizeof(dbx),digest));
    puts("BOOT SHA-256 and authenticated database parser PASS");return 0;
}
