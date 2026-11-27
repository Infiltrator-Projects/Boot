/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_trust.h"

/* FIPS 180-4 SHA-256, independent of libc and firmware hash availability.
 * This is an integrity primitive; only the platform's authenticated db/dbx
 * policy below supplies authority. All arithmetic is unsigned modulo 2^32. */
static const uint32_t k[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static uint32_t rr(uint32_t x,unsigned n) {return (x>>n)|(x<<(32-n));}
static uint32_t be32(const uint8_t *p)
{return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void compress(uint32_t h[8],const uint8_t p[64])
{
    uint32_t w[64],a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
    unsigned i;
    for(i=0;i<16;i++) w[i]=be32(p+4*i);
    for(;i<64;i++) {
        uint32_t x=w[i-15],y=w[i-2];
        w[i]=w[i-16]+(rr(x,7)^rr(x,18)^(x>>3))+w[i-7]+(rr(y,17)^rr(y,19)^(y>>10));
    }
    for(i=0;i<64;i++) {
        uint32_t t1=z+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
        uint32_t t2=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));
        z=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
}
void boot_v2_sha256(const void *data,size_t n,uint8_t out[32])
{
    const uint8_t *p=data;
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint8_t tail[64]={0};
    uint64_t bits=(uint64_t)n*8;
    unsigned i;
    while(n>=64) {compress(h,p);p+=64;n-=64;}
    for(i=0;i<n;i++) tail[i]=p[i];
    tail[n]=0x80;
    if(n>=56) {compress(h,tail);for(i=0;i<64;i++) tail[i]=0;}
    for(i=0;i<8;i++) tail[63-i]=(uint8_t)(bits>>(8*i));
    compress(h,tail);
    for(i=0;i<32;i++) out[i]=(uint8_t)(h[i/4]>>(24-8*(i%4)));
}
static uint32_t le32(const uint8_t *p)
{return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int equal(const uint8_t *a,const uint8_t *b,size_t n)
{
    uint8_t diff=0;while(n--) diff|=*a++^*b++;return !diff;
}
/* EFI_CERT_SHA256_GUID in little-endian EFI_GUID wire order. Each entry
 * comprises a 16-byte owner GUID and the raw 32-byte digest. No unaligned
 * structure reads; checked subtraction prevents integer wrap. */
static const uint8_t sha_type[16]={0x26,0x16,0xc4,0xc1,0x4c,0x50,0x92,0x40,0xac,0xa9,0x41,0xf9,0x36,0x93,0x43,0x28};
static int database(const void *data,size_t n,const uint8_t digest[32])
{
    const uint8_t *p=data;
    int found=0;
    if(!p && n) return -1;
    while(n) {
        uint32_t list,header,entry;
        size_t remain,at;
        int sha;
        if(n<28) return -1;
        list=le32(p+16);header=le32(p+20);entry=le32(p+24);
        if(list<28 || list>n || header>list-28 || entry<16) return -1;
        remain=list-28-header;
        if(!remain || remain%entry) return -1;
        sha=equal(p,sha_type,16);
        if(sha && (header || entry!=48)) return -1;
        if(sha) for(at=28+header;at<list;at+=entry)
            found|=equal(p+at+16,digest,32);
        p+=list;n-=list;
    }
    return found;
}
int boot_v2_trust_digest(const void *db,size_t dbn,const void *dbx,size_t dbxn,const uint8_t digest[32])
{
    if(!digest || database(db,dbn,digest)!=1 || database(dbx,dbxn,digest)!=0) return -1;
    return 0;
}
