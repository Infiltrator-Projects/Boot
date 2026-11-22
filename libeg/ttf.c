/* Firmware TrueType rendering from packaged MB/Daimler font faces.
 * Validates font tables, rasterises glyphs and caches scale-dependent masks.
 * Packaging verifies checksums; runtime validation is structural.
 */

#include "libegint.h"
#include "../boot/global.h"
#include "../boot/log.h"
#include <stddef.h>

#define MB_FONT_COUNT 23
#define MB_FONT_ROLE_COUNT 3
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
/* Upstream's font-name helper contains two direct memcpy calls rather
 * than STBTT_memcpy. Bind those to the same firmware-local copy routine. */
#define memcpy mbCopy
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#undef memcpy

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

/* All 23 verified fonts from InfiltratorOS/assets/fonts. The three initial
 * UI role mappings are provisional display defaults, not a font selection
 * decision: boot.conf may map every role to any registered face. */
static MB_FACE mbFaces[MB_FONT_COUNT] = {
    { .path = L"fonts\\daimler_cac_app_regular.ttf" },
    { .path = L"fonts\\daimler_cac_regular.ttf" },
    { .path = L"fonts\\daimler_cs_app_demi.ttf" },
    { .path = L"fonts\\daimler_cs_app_regular.ttf" },
    { .path = L"fonts\\daimler_cs_demi.ttf" },
    { .path = L"fonts\\daimler_cs_regular.ttf" },
    { .path = L"fonts\\mb_corpo_a_screen_1_cond_regular.ttf" },
    { .path = L"fonts\\mb_corpo_a_screen_2_cond_regular.ttf" },
    { .path = L"fonts\\mb_corpo_a_text_cond_regular.ttf" },
    { .path = L"fonts\\mb_corpo_a_title_cond_regular.ttf" },
    { .path = L"fonts\\mb_corpo_a_title_regular.ttf" },
    { .path = L"fonts\\mb_corpo_s_text_bold.ttf" },
    { .path = L"fonts\\mb_corpo_s_text_cond_bold.ttf" },
    { .path = L"fonts\\mb_corpo_s_text_cond_light.ttf" },
    { .path = L"fonts\\mb_corpo_s_text_cond_regular.ttf" },
    { .path = L"fonts\\mb_corpo_s_text_light.ttf" },
    { .path = L"fonts\\mb_corpo_s_text_regular.ttf" },
    { .path = L"fonts\\mb_corpo_s_title_bold.ttf" },
    { .path = L"fonts\\mb_corpo_s_title_cond_bold.ttf" },
    { .path = L"fonts\\mb_corpo_s_title_cond_light.ttf" },
    { .path = L"fonts\\mb_corpo_s_title_cond_regular.ttf" },
    { .path = L"fonts\\mb_corpo_s_title_light.ttf" },
    { .path = L"fonts\\mb_corpo_s_title_regular.ttf" }
};
static UINTN mbRoleFace[MB_FONT_ROLE_COUNT] = { 16, 11, 10 };
static UINTN mbLoadedCount;
static BOOLEAN mbRasterisedGlyph;
static BOOLEAN mbInitialized;
static UINTN mbCurrentPixelHeight;
static UINTN mbLineHeight = 20;
static UINTN mbAverageAdvance = 10;

static UINT16 mb16(const UINT8 *p) { return (UINT16)((UINT16)p[0]<<8 | p[1]); }
static UINT32 mb32(const UINT8 *p) {
    return ((UINT32)p[0]<<24)|((UINT32)p[1]<<16)|((UINT32)p[2]<<8)|p[3];
}
static BOOLEAN mbWithin(UINTN start, UINTN length, UINTN total) {
    return start <= total && length <= total - start;
}
/* Find a TrueType table by tag after validating directory and table bounds.
 * Pointers into the supplied font buffer remain borrowed.
 */
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
/* Decode one Unicode scalar from a borrowed, terminated UTF-16 cursor.
 * A valid surrogate pair consumes two code units; an isolated surrogate
 * consumes one and substitutes '?'. Zero ends iteration without advancing.
 */
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
/* Match only a registered basename. Paths, extensions outside the inventory,
 * and arbitrary EFI volume files cannot be used as selectable font resources. */
BOOLEAN egSelectFontRole(IN UINTN role, IN CHAR16 *filename) {
    UINTN i;
    if (role >= MB_FONT_ROLE_COUNT || !filename) return FALSE;
    for (i=0; i<MB_FONT_COUNT; ++i) {
        if (StrCmp(filename, mbFaces[i].path + 6) == 0) {
            mbRoleFace[role] = i;
            return TRUE;
        }
    }
    return FALSE;
}
static MB_FACE *mbFace(UINTN role) {
    UINTN idx;
    if (role >= MB_FONT_ROLE_COUNT) role = MB_FONT_BODY;
    idx = mbRoleFace[role];
    if (mbFaces[idx].ready) return &mbFaces[idx];
    return NULL;
}
/*
 * UEFI's GOP mode may change after fonts are initially loaded (SetupScreen
 * and external EFI programs). Parsed outlines are reusable, but pixel-size-
 * dependent cached glyphs must be freed before changing rasterisation scale.
 */
static VOID mbUpdateScreenScale(VOID) {
    UINTN i,j,screenWidth,screenHeight,pixels;
    egGetScreenSize(&screenWidth,&screenHeight);
    /* Scale footer/menu labels with physical GOP resolution, rather than
     * holding them at 18 or 30 pixels on every display. */
    pixels = screenWidth / 60;
    if (pixels > screenHeight / 26) pixels = screenHeight / 26;
    if (pixels < 22) pixels = 22;
    if (pixels > 56) pixels = 56;
    if (mbCurrentPixelHeight == pixels) return;
    for (i=0; i<MB_FONT_COUNT; ++i) {
        MB_FACE *f=&mbFaces[i];
        for (j=0; j<MB_GLYPH_ASCII_CACHE; ++j) {
            mbFree(f->ascii[j].mask);
            mbSet(&f->ascii[j],0,sizeof(f->ascii[j]));
        }
        if (f->ready) {
            f->scale=stbtt_ScaleForPixelHeight(&f->font,(float)pixels);
            stbtt_GetFontVMetrics(&f->font,&f->ascent,NULL,NULL);
        }
    }
    mbCurrentPixelHeight=pixels;
    mbLineHeight=pixels+pixels/5;
    mbAverageAdvance=pixels*3/5;
    LOG(1,LOG_LINE_NORMAL,L"MB TrueType renderer scale: %d px at %dx%d",pixels,screenWidth,screenHeight);
}

/*
 * One-time font-file discovery reads all registered faces from SelfDir.
 * Successful parsing transfers each owned byte buffer to its MB_FACE, where
 * stbtt_fontinfo retains pointers into it. Failed faces release their input.
 * mbInitialized is set before probing; a later SelfDir becoming available
 * does not automatically retry missing fonts. Scale changes invalidate only
 * glyph bitmaps, not the resident outline/font tables.
 */
static VOID mbPrepare(VOID) {
    UINTN i;
    if (!mbInitialized) {
        mbInitialized=TRUE;
        for (i=0; i<MB_FONT_COUNT; ++i) {
            MB_FACE *f=&mbFaces[i];
            UINT8 *buf=NULL;
            UINTN len=0;
            EFI_STATUS status;
            if (!SelfDir) continue;
            status=egLoadFile(SelfDir,f->path,&buf,&len);
            if (EFI_ERROR(status) || !buf) continue;
            if (!mbValidateTtf(buf,len) ||
                !stbtt_InitFont(&f->font,buf,stbtt_GetFontOffsetForIndex(buf,0))) {
                mbFree(buf);
                continue;
            }
            f->data=buf;
            f->length=len;
            f->ready=TRUE;
            ++mbLoadedCount;
        }
    }
    mbUpdateScreenScale();
}
/* Ensure codepoint substitution and kerning agree between layout and draw. */
static UINT32 mbSupportedCodepoint(MB_FACE *f, UINT32 cp) {
    return cp && !stbtt_FindGlyphIndex(&f->font,(int)cp) ? (UINT32)'?' : cp;
}
/*
 * Probe each configured face using the actual firmware rasteriser on an
 * off-screen pixel buffer. Detects blank glyphs and invalid role mappings
 * without modifying the BOOT banner or producing diagnostic text onscreen.
 */
BOOLEAN egFontReady(VOID) {
    UINTN role;
    EG_PIXEL black = {0,0,0,0};
    mbPrepare();
    if (mbLoadedCount != MB_FONT_COUNT) return FALSE;
    for (role=0; role<MB_FONT_ROLE_COUNT; ++role) {
        EG_IMAGE *scratch=egCreateFilledImage(160,64,FALSE,&black);
        UINTN i,count;
        BOOLEAN changed=FALSE;
        if (!scratch) return FALSE;
        egRenderTextRole(L"BOOT",scratch,3,3,0,role);
        count=scratch->Width*scratch->Height;
        for (i=0; i<count; ++i) {
            EG_PIXEL *p=&scratch->PixelData[i];
            if (p->r || p->g || p->b) { changed=TRUE; break; }
        }
        egFreeImage(scratch);
        if (!changed) {
            LOG(1,LOG_LINE_NORMAL,L"MB TrueType font role %d failed glyph test",role);
            return FALSE;
        }
        LOG(1,LOG_LINE_NORMAL,L"MB TrueType font role %d rendered using %s",
            role,mbFaces[mbRoleFace[role]].path);
    }
    return TRUE;
}
/* Return the number of successfully prepared font faces.
 */
UINTN egGetFontLoadedCount(VOID) {
    mbPrepare();
    return mbLoadedCount;
}
/* Return the prepared line height in device pixels.
 */
UINTN egGetFontHeight(VOID) {
    mbPrepare();
    return mbLineHeight;
}
/* Kept for legacy horizontal margins only, never for glyph positioning. */
UINTN egGetFontCellWidth(VOID) {
    mbPrepare();
    return mbAverageAdvance;
}
/*
 * Layout uses the same substitute glyph and kerning pairs as drawing, so
 * measured advances reflect the selected face instead of fixed cell counts.
 * A face absent at preparation time falls back to an estimated width;
 * that fallback cannot establish which physical glyphs will be visible.
 */
UINTN egComputeTextWidthRole(IN CHAR16 *text, IN UINTN role) {
    MB_FACE *f;
    const CHAR16 *p=text;
    float width=0;
    UINT32 cp, next;
    if (!text) return 0;
    mbPrepare();
    f=mbFace(role);
    if (!f) return StrLen(text)*mbAverageAdvance;
    cp=mbSupportedCodepoint(f,mbNextCodepoint(&p));
    while (cp) {
        int advance=0;
        next=mbSupportedCodepoint(f,mbNextCodepoint(&p));
        stbtt_GetCodepointHMetrics(&f->font,(int)cp,&advance,NULL);
        width += (float)advance*f->scale;
        if (next) width += (float)stbtt_GetCodepointKernAdvance(&f->font,(int)cp,(int)next)*f->scale;
        cp=next;
    }
    if (width<=0.0f) return 0;
    if (width>(float)((UINTN)-1)/2.0f) return (UINTN)-1;
    return (UINTN)(width+1.5f);
}
/* Measure body-role text using the same advances as rendering.
 */
UINTN egComputeTextWidth(IN CHAR16 *text) {
    return egComputeTextWidthRole(text,MB_FONT_BODY);
}
/* Return measured body-role width and current font line height.
 * NULL output pointers are permitted.
 */
VOID egMeasureText(IN CHAR16 *text, OUT UINTN *width, OUT UINTN *height) {
    if (width) *width=egComputeTextWidth(text);
    if (height) *height=egGetFontHeight();
}
/*
 * Rasterise one codepoint into an owned 8-bit coverage mask. A missing glyph
 * substitutes '?'; empty outlines are valid zero-area glyphs. Bitmap width
 * and height have independent 256-pixel caps, and a failed allocation leaves
 * the glyph unprepared. The caller owns the resulting mask unless it is
 * retained in the face's ASCII cache.
 */
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
/* Rasterise a selected TrueType face into a borrowed RGBA image at its
 * current role scale. Glyph masks are face-owned when cached, otherwise
 * temporary; each pixel is clipped to image dimensions before blending.
 * The brightness hint selects ink contrast but does not own the image.
 */
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
        UINT32 rendered=mbSupportedCodepoint(f,cp);
        UINTN y,x;
        INTN left,top;
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
        if (!mbRasterisedGlyph && g->mask && g->w && g->h) {
            UINTN k;
            for (k=0; k<g->w*g->h; ++k) {
                if (g->mask[k]) {
                    mbRasterisedGlyph = TRUE;
                    LOG(1, LOG_LINE_NORMAL, L"MB TrueType glyph rasterised by EFI renderer");
                    break;
                }
            }
        }
        if (g==&temp) mbFree(g->mask);
        stbtt_GetCodepointHMetrics(&f->font,(int)rendered,&advance,NULL);
        next=mbNextCodepoint(&p);
        pen+=(float)advance*f->scale;
        if (next) pen+=(float)stbtt_GetCodepointKernAdvance(&f->font,(int)rendered,(int)mbSupportedCodepoint(f,next))*f->scale;
        cp=next;
    }
}
/* Render text with the body font onto a borrowed image.
 * Pixel allocation and cache ownership stay with the renderer.
 */
VOID egRenderText(IN CHAR16 *text, IN OUT EG_IMAGE *image,
                  IN UINTN px, IN UINTN py, IN UINT8 brightness) {
    egRenderTextRole(text,image,px,py,brightness,MB_FONT_BODY);
}

/*
 * Large product identity uses the same verified Type2 rasteriser as body text.
 * Scale is temporarily increased; ASCII cache masks belong to one size only,
 * so they are always cleared on entry and restoration. BOOT is single-threaded
 * while constructing its graphical menu and callers cannot interleave draws.
 */
static VOID mbClearFaceCache(MB_FACE *face) {
    UINTN i;
    for(i=0;i<MB_GLYPH_ASCII_CACHE;++i) {
        mbFree(face->ascii[i].mask);
        mbSet(&face->ascii[i],0,sizeof(face->ascii[i]));
    }
}
/* Measure a role's real font advances at a requested pixel height without
 * changing its long-lived default scale. The temporary scale is restored
 * before returning; width zero also represents missing face/invalid size.
 */
UINTN egComputeTextWidthAtPixelHeight(IN CHAR16 *text, IN UINTN role,
                                       IN UINTN pixels) {
    MB_FACE *face;
    float prior;
    UINTN width;
    if(!text || pixels<8 || pixels>160)return 0;
    mbPrepare();
    face=mbFace(role);
    if(!face)return 0;
    prior=face->scale;
    face->scale=stbtt_ScaleForPixelHeight(&face->font,(float)pixels);
    width=egComputeTextWidthRole(text,role);
    face->scale=prior;
    return width;
}
/* Draw a temporary, explicitly sized title face, preserving the role's
 * previous scale. ASCII glyph caches are cleared before and after the draw
 * because cached coverage masks encode the earlier rasterisation scale.
 * This shared face state assumes serialized menu construction, not threads.
 */
VOID egRenderTextAtPixelHeight(IN CHAR16 *text, IN OUT EG_IMAGE *image,
                               IN UINTN x, IN UINTN y, IN UINT8 brightness,
                               IN UINTN role, IN UINTN pixels) {
    MB_FACE *face;
    float prior;
    if(!text || !image || pixels<8 || pixels>160)return;
    mbPrepare();
    face=mbFace(role);
    if(!face)return;
    prior=face->scale;
    mbClearFaceCache(face);
    face->scale=stbtt_ScaleForPixelHeight(&face->font,(float)pixels);
    egRenderTextRole(text,image,x,y,brightness,role);
    mbClearFaceCache(face);
    face->scale=prior;
}
