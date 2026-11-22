#!/usr/bin/env bash
# BOOT / Host-side image-scaler regression
#
# Compile the production bilinear scaler with minimal EFI-compatible host
# substitutes, exercising both display-resolution arithmetic and singleton
# source axes. A previous truncated fixed-point ratio wrapped unsigned
# fractions and produced patterned stray pixels on scaled wallpapers.
set -euo pipefail
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
{
cat <<'HEAD'
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef size_t UINTN;
typedef uint8_t UINT8;
typedef uint8_t BOOLEAN;
typedef struct { UINT8 b, g, r, a; } EG_PIXEL;
typedef struct { UINTN Width, Height; BOOLEAN HasAlpha; EG_PIXEL *PixelData; } EG_IMAGE;
#define IN
#define LOG(...) ((void)0)
#define LOG_LINE_NORMAL 0
#define FP_MULTIPLIER ((UINTN)65536)
static EG_IMAGE *egCreateImage(UINTN w, UINTN h, BOOLEAN alpha) {
    EG_IMAGE *im=calloc(1,sizeof(*im));
    if (!im) return NULL;
    im->PixelData=calloc(w*h,sizeof(EG_PIXEL));
    if (!im->PixelData) { free(im); return NULL; }
    im->Width=w; im->Height=h; im->HasAlpha=alpha;
    return im;
}
static EG_IMAGE *egCopyImage(EG_IMAGE *orig) {
    EG_IMAGE *im=egCreateImage(orig->Width,orig->Height,orig->HasAlpha);
    if (im) memcpy(im->PixelData,orig->PixelData,
                   orig->Width*orig->Height*sizeof(EG_PIXEL));
    return im;
}
static void release(EG_IMAGE *im) {
    if (im) {free(im->PixelData);free(im);}
}
HEAD
awk '/^EG_IMAGE \* egScaleImage\(/ { on=1 } on {print} on && /^}/ {exit}' libeg/image.c
cat <<'MAIN'
#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr,"image scaler regression at line %d: %s\n",__LINE__,#test); \
    return 1; }} while(0)
static int between(UINT8 out,UINT8 a,UINT8 b,UINT8 c,UINT8 d) {
    UINT8 lo=a,hi=a;
    if (b < lo) lo = b;
    if (b > hi) hi = b;
    if (c < lo) lo = c;
    if (c > hi) hi = c;
    if (d < lo) lo = d;
    if (d > hi) hi = d;
    return out>=lo && out<=hi;
}
int main(void) {
    const UINTN sw=1648,sh=928,dw=1920,dh=1080;
    EG_IMAGE *source=egCreateImage(sw,sh,0),*scaled,*one,*expanded;
    UINTN x,y;
    CHECK(source!=NULL);
    for(y=0;y<sh;y++) for(x=0;x<sw;x++) {
        EG_PIXEL *p=&source->PixelData[y*sw+x];
        p->r=(UINT8)(x*255/(sw-1));
        p->g=(UINT8)(y*255/(sh-1));
        p->b=(UINT8)((x+y)%256);
        p->a=0;
    }
    scaled=egScaleImage(source,dw,dh);
    CHECK(scaled!=NULL);
    for(y=0;y<dh;y++) for(x=0;x<dw;x++) {
        UINTN xx=x*(sw-1)/dw,yy=y*(sh-1)/dh;
        EG_PIXEL *p=&scaled->PixelData[y*dw+x],
                 *a=&source->PixelData[yy*sw+xx],
                 *b=&source->PixelData[yy*sw+xx+1],
                 *c=&source->PixelData[(yy+1)*sw+xx],
                 *d=&source->PixelData[(yy+1)*sw+xx+1];
        CHECK(between(p->r,a->r,b->r,c->r,d->r));
        CHECK(between(p->g,a->g,b->g,c->g,d->g));
        CHECK(between(p->b,a->b,b->b,c->b,d->b));
        CHECK(p->a==0);
    }
    release(scaled);
    release(source);
    one=egCreateImage(1,1,1);
    CHECK(one!=NULL);
    one->PixelData[0]=(EG_PIXEL){77,66,55,123};
    expanded=egScaleImage(one,23,17);
    CHECK(expanded!=NULL);
    for(x=0;x<23*17;x++) {
        EG_PIXEL p=expanded->PixelData[x];
        CHECK(p.r==55 && p.g==66 && p.b==77 && p.a==123);
    }
    release(expanded);
    release(one);
    puts("host image scaler regressions passed");
    return 0;
}
MAIN
} > "$work/scale-test.c"
grep -Fq 'EG_IMAGE * egScaleImage(' "$work/scale-test.c"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror "$work/scale-test.c" -o "$work/scale-test"
"$work/scale-test"
