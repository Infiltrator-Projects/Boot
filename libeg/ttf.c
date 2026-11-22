/*
 * BOOT firmware TrueType text renderer.
 *
 * Loads the verified MB Corpo outlines installed beside BOOTX64.EFI and
 * rasterises glyphs directly into EG_IMAGE surfaces. No pre-rendered font
 * atlas, ImageMagick, host font subsystem, or runtime libc is required.
 *
 * stb_truetype is included as an independently licensed single-header
 * outline parser/rasteriser. The wrapper supplies firmware allocations,
 * bounded font loading and mathematical primitives without libm.
 */
#include "libegint.h"
#include "../boot/global.h"
#include <stddef.h>

#define MB_FONT_COUNT 3
#define MB_FONT_MAX_BYTES (2U * 1024U * 1024U)
#define MB_GLYPH_MAX_EDGE 256
#define MB_GLYPH_ASCII_CACHE 128

static VOID *mbAlloc(UINTN size) { return size ? AllocatePool(size) : NULL; }
static VOID mbFree(VOID *p) { if (p) FreePool(p); }
static VOID *mbCopy(VOID *dst, const VOID *src, UINTN n) {
    UINT8 *d = (UINT8 *)dst;
    const UINT8 *s = (const UINT8 *)src;
    UINTN i;
    for (i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}
static VOID *mbSet(VOID *dst, int value, UINTN n) {
    UINT8 *d = (UINT8 *)dst;
    UINTN i;
    for (i = 0; i < n; ++i) d[i] = (UINT8)value;
    return dst;
}
static UINTN mbLen(const char *s) { UINTN n=0; while (s[n]) ++n; return n; }

/* No hosted libm symbols: these are only the operations used by stb's
 * optional signed-distance and outline helpers. Iteration bounds are fixed. */
static double mbSqrt(double v) {
    double x;
    int i;
    if (v <= 0.0) return 0.0;
    x = v >= 1.0 ? v : 1.0;
    for (i = 0; i < 32; ++i) x = 0.5 * (x + v / x);
    return x;
}
static double mbCbrt(double v) {
    double x;
    int i;
    if (v <= 0.0) return 0.0;
    x = v >= 1.0 ? v : 1.0;
    for (i = 0; i < 48; ++i) x = (2.0 * x + v / (x*x)) / 3.0;
    return x;
}
static double mbCos(double a) {
    const double pi = 3.14159265358979323846;
    double term = 1.0, sum = 1.0;
    int i;
    while (a > pi) a -= 2.0*pi;
    while (a < -pi) a += 2.0*pi;
    for (i = 1; i <= 14; ++i) {
        term *= -a*a / ((2.0*i - 1.0)*(2.0*i));
        sum += term;
    }
    return sum;
}
static double mbAcos(double v) {
    double lo = 0.0, hi = 3.14159265358979323846;
    int i;
    if (v >= 1.0) return 0.0;
    if (v <= -1.0) return hi;
    for (i = 0; i < 40; ++i) {
        double mid = 0.5*(lo+hi);
        if (mbCos(mid) > v) lo = mid;
        else hi = mid;
    }
    return 0.5*(lo+hi);
}
static double mbMod(double a, double b) {
    long q;
    if (b == 0.0) return 0.0;
    q = (long)(a/b);
    return a - (double)q*b;
}
static int mbFloor(double x) { int i=(int)x; return i > x ? i-1 : i; }
static int mbCeil(double x) { int i=(int)x; return i < x ? i+1 : i; }
static double mbAbs(double x) { return x < 0.0 ? -x : x; }

/* stb's only STBTT_pow callsites calculate cubic roots. */
#define STBTT_pow(x,y) ((VOID)(y), mbCbrt(x))
#define STBTT_sqrt(x) mbSqrt(x)
#define STBTT_cos(x) mbCos(x)
#define STBTT_acos(x) mbAcos(x)
#define STBTT_fmod(x,y) mbMod((x),(y))
#define STBTT_ifloor(x) mbFloor(x)
#define STBTT_iceil(x) mbCeil(x)
#define STBTT_fabs(x) mbAbs(x)
#define STBTT_malloc(sz,ctx) ((VOID)(ctx), mbAlloc((UINTN)(sz)))
#define STBTT_free(ptr,ctx) ((VOID)(ctx), mbFree(ptr))
#define STBTT_assert(x) ((VOID)0)
#define STBTT_strlen(x) mbLen(x)
#define STBTT_memcpy mbCopy
#define STBTT_memset mbSet
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

typedef struct {
    UINT8 *mask;
    INTN xoff, yoff;
    UINTN w, h;
    BOOLEAN prepared;
} MB_GLYPH;

typedef struct {
    CHAR16 *path;
    UINT8 *data;
    UINTN length;
    stbtt_fontinfo font;
    float scale;
    int ascent;
    BOOLEAN ready;
    MB_GLYPH ascii[MB_GLYPH_ASCII_CACHE];
} MB_FACE;

static MB_FACE mbFaces[MB_FONT_COUNT] = {
    { .path = L"fonts\\mb_corpo_s_text_regular.ttf" },
    { .path = L"fonts\\mb_corpo_s_text_bold.ttf" },
    { .path = L"fonts\\mb_corpo_a_title_cond_regular.ttf" }
};
static BOOLEAN mbInitialized;
static UINTN mbLineHeight = 20;
static UINTN mbAverageAdvance = 10;

static UINT16 mb16(const UINT8 *p) { return (UINT16)((UINT16)p[0]<<8 | p[1]); }
static UINT32 mb32(const UINT8 *p) {
    return ((UINT32)p[0]<<24)|((UINT32)p[1]<<16)|((UINT32)p[2]<<8)|p[3];
}
static BOOLEAN mbWithin(UINTN start, UINTN length, UINTN total) {
    return start <= total && length <= total - start;
}
static BOOLEAN mbTable(const UINT8 *data, UINTN size, UINT32 tag,
                       const UINT8 **table, UINTN *length) {
    UINTN n, i;
    if (size < 12) return FALSE;
    n = mb16(data+4);
    if (n == 0 || n > 128 || !mbWithin(12, n*16, size)) return FALSE;
    for (i=0; i<n; ++i) {
        const UINT8 *entry = data + 12 + 16*i;
        UINTN off = mb32(entry+8), len = mb32(entry+12);
        if (!mbWithin(off,len,size)) return FALSE;
        if (mb32(entry) == tag) {
            *table=data+off; *length=len; return TRUE;
        }
    }
    return FALSE;
}
#define MB_TAG(a,b,c,d) (((UINT32)(a)<<24)|((UINT32)(b)<<16)|((UINT32)(c)<<8)|(d))

/*
 * Reject malformed or implausibly large TrueType tables before invoking
 * stb's optimised glyph parser, which assumes already validated table input.
 */
static BOOLEAN mbValidateTtf(const UINT8 *bytes, UINTN size) {
    const UINT8 *head,*maxp,*hhea,*hmtx,*loca,*glyf,*cmap;
    UINTN nh, nm, naa, nhmtx, nloca, nglyf, ncmap, i;
    UINTN glyphs, metrics, locFormat, prior = 0;
    if (size < 64 || size > MB_FONT_MAX_BYTES || mb32(bytes) != 0x00010000U)
        return FALSE; /* Only glyf-based TTF, not CFF/OTTO or TTC. */
    if (!mbTable(bytes,size,MB_TAG('h','e','a','d'),&head,&nh) || nh<54 ||
        !mbTable(bytes,size,MB_TAG('m','a','x','p'),&maxp,&nm) || nm<6 ||
        !mbTable(bytes,size,MB_TAG('h','h','e','a'),&hhea,&naa) || naa<36 ||
        !mbTable(bytes,size,MB_TAG('h','m','t','x'),&hmtx,&nhmtx) ||
        !mbTable(bytes,size,MB_TAG('l','o','c','a'),&loca,&nloca) ||
        !mbTable(bytes,size,MB_TAG('g','l','y','f'),&glyf,&nglyf) ||
        !mbTable(bytes,size,MB_TAG('c','m','a','p'),&cmap,&ncmap) || ncmap<4)
        return FALSE;
    glyphs=mb16(maxp+4);
    metrics=mb16(hhea+34);
    locFormat=mb16(head+50);
    if (!mb16(head+18) || !glyphs || glyphs>32768 || !metrics ||
        metrics>glyphs || locFormat>1 ||
        !mbWithin(0,4*metrics + 2*(glyphs-metrics),nhmtx) ||
        !mbWithin(0,(glyphs+1)*(locFormat?4:2),nloca))
        return FALSE;
    for (i=0; i<=glyphs; ++i) {
        UINTN off=locFormat ? mb32(loca+4*i) : 2*mb16(loca+2*i);
        if (off<prior || off>nglyf) return FALSE;
        prior=off;
    }
    return TRUE;
}
static UINT32 mbNextCodepoint(const CHAR16 **it) {
    UINT32 a = (UINT16)**it;
    if (!a) return 0;
    ++*it;
    if (a >= 0xd800 && a <= 0xdbff) {
        UINT32 b=(UINT16)**it;
        if (b >= 0xdc00 && b <= 0xdfff) {
            ++*it;
            return 0x10000 + ((a-0xd800)<<10) + (b-0xdc00);
        }
        return '?';
    }
    if (a>=0xdc00 && a<=0xdfff) return '?';
    return a;
}
static MB_FACE *mbFace(UINTN role) {
    if (role >= MB_FONT_COUNT) role=0;
    if (mbFaces[role].ready) return &mbFaces[role];
    if (mbFaces[0].ready) return &mbFaces[0];
    return NULL;
}
static VOID mbPrepare(VOID) {
    UINTN i, sw, sh, pixels;
    if (mbInitialized) return;
    mbInitialized = TRUE;
    egGetScreenSize(&sw, &sh);
    pixels = sw >= HIDPI_MIN ? 30 : 18;
    mbLineHeight = pixels + (pixels/5);
    mbAverageAdvance = pixels*3/5;
    for (i=0; i<MB_FONT_COUNT; ++i) {
        MB_FACE *f=&mbFaces[i];
        UINT8 *buf=NULL;
        UINTN len=0;
        EFI_STATUS status;
        if (!SelfDir) continue;
        status=egLoadFile(SelfDir, f->path, &buf, &len);
        if (EFI_ERROR(status) || !buf) continue;
        if (!mbValidateTtf(buf,len) ||
            !stbtt_InitFont(&f->font,buf,stbtt_GetFontOffsetForIndex(buf,0))) {
            mbFree(buf);
            continue;
        }
        f->data=buf;
        f->length=len;
        f->scale=stbtt_ScaleForPixelHeight(&f->font,(float)pixels);
        stbtt_GetFontVMetrics(&f->font,&f->ascent,NULL,NULL);
        f->ready=TRUE;
    }
}
BOOLEAN egFontReady(VOID) {
    mbPrepare();
    return mbFaces[0].ready;
}
UINTN egGetFontHeight(VOID) {
    mbPrepare();
    return mbLineHeight;
}
/* Kept for legacy horizontal margins only, never for glyph positioning. */
UINTN egGetFontCellWidth(VOID) {
    mbPrepare();
    return mbAverageAdvance;
}
UINTN egComputeTextWidthRole(IN CHAR16 *text, IN UINTN role) {
    MB_FACE *f;
    const CHAR16 *p=text;
    float width=0;
    UINT32 cp, next;
    if (!text) return 0;
    mbPrepare();
    f=mbFace(role);
    if (!f) return StrLen(text)*mbAverageAdvance;
    cp=mbNextCodepoint(&p);
    while (cp) {
        int advance=0;
        next=mbNextCodepoint(&p);
        if (!stbtt_FindGlyphIndex(&f->font,(int)cp)) cp='?';
        stbtt_GetCodepointHMetrics(&f->font,(int)cp,&advance,NULL);
        width += (float)advance*f->scale;
        if (next) width += (float)stbtt_GetCodepointKernAdvance(&f->font,(int)cp,(int)next)*f->scale;
        cp=next;
    }
    if (width<=0.0f) return 0;
    if (width>(float)((UINTN)-1)/2.0f) return (UINTN)-1;
    return (UINTN)(width+1.5f);
}
UINTN egComputeTextWidth(IN CHAR16 *text) {
    return egComputeTextWidthRole(text,MB_FONT_BODY);
}
VOID egMeasureText(IN CHAR16 *text, OUT UINTN *width, OUT UINTN *height) {
    if (width) *width=egComputeTextWidth(text);
    if (height) *height=egGetFontHeight();
}
static BOOLEAN mbBuildGlyph(MB_FACE *f, UINT32 cp, MB_GLYPH *g) {
    int x0,y0,x1,y1;
    UINTN w,h;
    if (!stbtt_FindGlyphIndex(&f->font,(int)cp)) cp='?';
    stbtt_GetCodepointBitmapBox(&f->font,(int)cp,f->scale,f->scale,&x0,&y0,&x1,&y1);
    if (x1<x0 || y1<y0) return FALSE;
    w=(UINTN)(x1-x0);
    h=(UINTN)(y1-y0);
    if (w>MB_GLYPH_MAX_EDGE || h>MB_GLYPH_MAX_EDGE) return FALSE;
    g->w=w; g->h=h; g->xoff=x0; g->yoff=y0;
    if (w && h) {
        g->mask=mbAlloc(w*h);
        if (!g->mask) return FALSE;
        stbtt_MakeCodepointBitmap(&f->font,g->mask,(int)w,(int)h,(int)w,
                                 f->scale,f->scale,(int)cp);
    }
    g->prepared=TRUE;
    return TRUE;
}
VOID egRenderTextRole(IN CHAR16 *text, IN OUT EG_IMAGE *image,
                      IN UINTN px, IN UINTN py, IN UINT8 brightness,
                      IN UINTN role) {
    MB_FACE *f;
    const CHAR16 *p=text;
    float pen;
    UINT32 cp,next;
    int baseline;
    if (!text || !image || !image->PixelData || px>=image->Width || py>=image->Height)
        return;
    mbPrepare();
    f=mbFace(role);
    if (!f) return;
    pen=(float)px;
    baseline=(int)py+(int)((float)f->ascent*f->scale+0.5f);
    cp=mbNextCodepoint(&p);
    while (cp && pen<(float)image->Width) {
        MB_GLYPH temp={0}, *g=&temp;
        int advance=0;
        UINT32 rendered=cp;
        UINTN y,x;
        INTN left,top;
        if (!stbtt_FindGlyphIndex(&f->font,(int)rendered)) rendered='?';
        if (rendered<MB_GLYPH_ASCII_CACHE)
            g=&f->ascii[rendered];
        if (!g->prepared && !mbBuildGlyph(f,rendered,g)) break;
        left=(INTN)pen+g->xoff;
        top=(INTN)baseline+g->yoff;
        for (y=0; y<g->h; ++y) {
            INTN destY=top+(INTN)y;
            if (destY<0 || (UINTN)destY>=image->Height) continue;
            for (x=0; x<g->w; ++x) {
                INTN destX=left+(INTN)x;
                UINT8 alpha,ink;
                EG_PIXEL *pixel;
                UINTN prior,outa,remain;
                if (destX<0 || (UINTN)destX>=image->Width) continue;
                alpha=g->mask[y*g->w+x];
                if (!alpha) continue;
                pixel=&image->PixelData[(UINTN)destY*image->Width+(UINTN)destX];
                ink=(brightness<128)?255:0;
                if (image->HasAlpha) {
                    prior=pixel->a;
                    remain=(255-alpha)*prior/255;
                    outa=alpha+remain;
                    if (outa) {
                        pixel->r=(UINT8)((ink*alpha+pixel->r*remain)/outa);
                        pixel->g=(UINT8)((ink*alpha+pixel->g*remain)/outa);
                        pixel->b=(UINT8)((ink*alpha+pixel->b*remain)/outa);
                        pixel->a=(UINT8)outa;
                    }
                } else {
                    pixel->r=(UINT8)((ink*alpha+pixel->r*(255-alpha))/255);
                    pixel->g=(UINT8)((ink*alpha+pixel->g*(255-alpha))/255);
                    pixel->b=(UINT8)((ink*alpha+pixel->b*(255-alpha))/255);
                }
            }
        }
        if (g==&temp) mbFree(g->mask);
        stbtt_GetCodepointHMetrics(&f->font,(int)rendered,&advance,NULL);
        next=mbNextCodepoint(&p);
        pen+=(float)advance*f->scale;
        if (next) pen+=(float)stbtt_GetCodepointKernAdvance(&f->font,(int)rendered,(int)next)*f->scale;
        cp=next;
    }
}
VOID egRenderText(IN CHAR16 *text, IN OUT EG_IMAGE *image,
                  IN UINTN px, IN UINTN py, IN UINT8 brightness) {
    egRenderTextRole(text,image,px,py,brightness,MB_FONT_BODY);
}
