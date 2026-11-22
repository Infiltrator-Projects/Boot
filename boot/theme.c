/*
 * BOOT's UEFI-native graphical theme.
 *
 * The official background and logo assets are configured through boot.conf.
 * Native PNG/JPEG decoding is optional at runtime. The BOOT title uses its
 * configured TrueType font; no vector graphics parser or converter is needed.
 *
 * All images allocated here are owned by this module or returned to a caller
 * that must release them; the cached background is borrowed and never passed
 * to egFreeImage by a caller. Resolution changes invalidate only that cache.
 */
#include "global.h"
#include "screen.h"
#include "theme.h"
#include "mystrings.h"
#include "log.h"


static EG_IMAGE *CachedBackground;
static UINTN CachedWidth, CachedHeight;

/* All visual assets are configured in boot.conf; no filename is compiled
 * into BOOT. The special value "none" disables an image without affecting
 * discovery, OS selection, or firmware handoff. */
static CHAR16 *themeImage(CHAR16 *configured) {
    return (configured && !MyStriCmp(configured, L"none")) ? configured : NULL;
}

EG_IMAGE *BootThemeBackground(UINTN width, UINTN height) {
    EG_IMAGE *loaded = NULL, *resized = NULL;
    CHAR16 *path = themeImage(GlobalConfig.ThemeBackgroundFileName);
    EG_PIXEL plain = {8, 11, 5, 0};

    if (!width || !height || width > 8192 || height > 8192) return NULL;
    /* Without any theme directives the original banner/graphics path remains
     * available, preserving existing boot.conf behaviour. */
    if (!GlobalConfig.ThemeBackgroundFileName &&
        !GlobalConfig.ThemeLogoFileName &&
        !GlobalConfig.ThemeMarkFileName &&
        !GlobalConfig.ThemeUnderlineFileName)
        return NULL;
    if (CachedBackground && width == CachedWidth && height == CachedHeight)
        return CachedBackground;

    if (path && SelfDir)
        loaded = egLoadImage(SelfDir, path, FALSE);
    if (loaded) {
        if (loaded->Width > 1 && loaded->Height > 1)
            resized = egScaleImage(loaded, width, height);
        egFreeImage(loaded);
    }
    /* Missing or disabled PNGs are not fatal. A plain canvas also lets the
     * remaining configured logo/text render when no wallpaper is supplied. */
    if (!resized) {
        resized = egCreateFilledImage(width, height, FALSE, &plain);
        if (resized)
            LOG(1, LOG_LINE_NORMAL, L"BOOT plain background selected");
    } else {
        LOG(1, LOG_LINE_NORMAL, L"BOOT configured PNG/JPEG background loaded");
    }
    if (!resized) return NULL;
    egFreeImage(CachedBackground);
    CachedBackground = resized;
    CachedWidth = width;
    CachedHeight = height;
    return CachedBackground;
}

/* Decode a configured mark/underline PNG. No vector paths or external
 * graphics converters participate in drawing these theme resources. */
static EG_IMAGE *loadWordmarkPng(CHAR16 *path, UINTN width, UINTN height) {
    EG_IMAGE *source, *scaled;
    if (!path || !SelfDir || width < 2 || height < 2) return NULL;
    source = egLoadImage(SelfDir, path, TRUE);
    if (!source) return NULL;
    if (source->Width < 2 || source->Height < 2) {
        egFreeImage(source);
        return NULL;
    }
    scaled = egScaleImage(source, width, height);
    egFreeImage(source);
    return scaled;
}

/*
 * theme_logo is an optional complete user PNG and needs no TrueType title.
 * Without it the configured PNG mark and underline surround live BOOT text.
 * Missing image components are simply skipped, not required by the runtime.
 */
UINTN BootThemeDrawWordmark(EG_IMAGE *background) {
    EG_IMAGE *letters, *region, *bars = NULL, *wave = NULL;
    CHAR16 *path;
    EG_PIXEL clear = {0, 0, 0, 0};
    UINTN size, wordWidth, slashWidth, slashHeight, waveWidth, waveHeight;
    UINTN gap, plateWidth, plateHeight, left, top, x, y;

    if (!background || background->Width < 400 || background->Height < 300)
        return 0;

    /* An explicitly disabled complete logo suppresses all wordmark drawing. */
    if (GlobalConfig.ThemeLogoFileName) {
        path = themeImage(GlobalConfig.ThemeLogoFileName);
        if (!path || !SelfDir) return 0;
        letters = egLoadImage(SelfDir, path, TRUE);
        if (!letters) return 0;
        if (letters->Width < 2 || letters->Height < 2 ||
            letters->Width > 8192 || letters->Height > 8192) {
            egFreeImage(letters);
            return 0;
        }
        plateWidth = letters->Width;
        plateHeight = letters->Height;
        if (plateWidth > background->Width * 3 / 4) {
            plateHeight = plateHeight * (background->Width * 3 / 4) / plateWidth;
            plateWidth = background->Width * 3 / 4;
        }
        if (plateHeight > background->Height / 3) {
            plateWidth = plateWidth * (background->Height / 3) / plateHeight;
            plateHeight = background->Height / 3;
        }
        if (plateWidth < 2 || plateHeight < 2) {
            egFreeImage(letters);
            return 0;
        }
        if (letters->Width != plateWidth || letters->Height != plateHeight) {
            EG_IMAGE *resized = egScaleImage(letters, plateWidth, plateHeight);
            egFreeImage(letters);
            if (!resized) return 0;
            letters = resized;
        }
        left = (background->Width - plateWidth) / 2;
        top = background->Height / 5;
        region = egCropImage(background, left, top, plateWidth, plateHeight);
        if (!region) {
            egFreeImage(letters);
            return 0;
        }
        egComposeImage(region, letters, 0, 0);
        egDrawImage(region, left, top);
        egFreeImage(region);
        egFreeImage(letters);
        LOG(1, LOG_LINE_NORMAL, L"BOOT configured complete PNG logo drawn");
        return top + plateHeight;
    }

    size = background->Width / 16;
    if (size > 116) size = 116;
    if (size < 44) size = 44;
    wordWidth = egComputeTextWidthAtPixelHeight(L"BOOT", MB_FONT_TITLE, size);
    if (!wordWidth || wordWidth > background->Width / 2) return 0;

    slashHeight = (size * 75) / 100;
    slashWidth = (slashHeight * 4) / 3;
    waveHeight = size / 6;
    if (waveHeight < 10) waveHeight = 10;
    waveWidth = (wordWidth * 92) / 100;
    gap = size / 8;

    path = themeImage(GlobalConfig.ThemeMarkFileName);
    if (path) bars = loadWordmarkPng(path, slashWidth, slashHeight);
    if (!bars) {
        slashWidth = 0;
        gap = 0;
    }

    path = themeImage(GlobalConfig.ThemeUnderlineFileName);
    if (path) wave = loadWordmarkPng(path, waveWidth, waveHeight);
    if (!wave) waveHeight = 0;

    plateWidth = slashWidth + gap + wordWidth + 12;
    plateHeight = size + waveHeight + 22;
    if (plateWidth > background->Width || plateHeight > background->Height) {
        egFreeImage(bars);
        egFreeImage(wave);
        return 0;
    }
    letters = egCreateFilledImage(plateWidth, plateHeight, TRUE, &clear);
    if (!letters) {
        egFreeImage(bars);
        egFreeImage(wave);
        return 0;
    }
    if (bars)
        egComposeImage(letters, bars, 0, (size - slashHeight) / 2);
    egRenderTextAtPixelHeight(L"BOOT", letters, slashWidth + gap, 2, 0,
                              MB_FONT_TITLE, size);
    /* Apply the silver lettering finish only to rasterised TrueType glyphs.
     * The mark and underline retain the source pixels of their PNG assets. */
    for (y = 0; y < size + 2; ++y)
        for (x = slashWidth + gap; x < plateWidth; ++x) {
            EG_PIXEL *pixel = &letters->PixelData[y * letters->Width + x];
            UINTN shade;
            if (!pixel->a) continue;
            shade = y < plateHeight / 3 ? 245 :
                    (y < plateHeight * 2 / 3 ? 214 : 233);
            pixel->r = (UINT8)shade;
            pixel->g = (UINT8)(shade + (245 - shade) / 3);
            pixel->b = (UINT8)(shade + (255 - shade) / 2);
        }
    if (wave)
        egComposeImage(letters, wave,
                       slashWidth + gap + (wordWidth - waveWidth) / 2, size + 2);
    egFreeImage(bars);
    egFreeImage(wave);

    left = (background->Width - plateWidth) / 2;
    top = background->Height / 5;
    region = egCropImage(background, left, top, plateWidth, plateHeight);
    if (!region) {
        egFreeImage(letters);
        return 0;
    }
    egComposeImage(region, letters, 0, 0);
    egDrawImage(region, left, top);
    egFreeImage(region);
    egFreeImage(letters);
    LOG(1, LOG_LINE_NORMAL, L"BOOT configured wordmark rendered");
    return top + plateHeight;
}

/* Transparent, smoked-graphite selection plate with a fine cyan highlight,
 * matched to Common's Night surfaces and the website's #27DCE9 accent.
 * Draw the actual OS icon on top using the existing menu compositor. */
static EG_IMAGE *makeThemePlate(UINTN size, BOOLEAN selected) {
    UINT8 red=selected?39:174;
    UINT8 green=selected?220:182;
    UINT8 blue=selected?233:189;
    EG_IMAGE *image;
    EG_PIXEL blank={0,0,0,0};
    UINTN x,y;
    INTN radius;
    if(size<32 || size>1024)return NULL;
    image=egCreateFilledImage(size,size,TRUE,&blank);
    if(!image)return NULL;
    radius=(INTN)(size/9);
    if(radius<5)radius=5;
    for(y=0;y<size;++y)for(x=0;x<size;++x) {
        INTN ix=(INTN)x,iy=(INTN)y;
        INTN low=(INTN)(size/14),high=(INTN)size-low-1;
        INTN dx=0,dy=0;
        INTN dist,edge;
        EG_PIXEL *p=&image->PixelData[y*size+x];
        if(ix<low+radius)dx=low+radius-ix;
        else if(ix>high-radius)dx=ix-(high-radius);
        if(iy<low+radius)dy=low+radius-iy;
        else if(iy>high-radius)dy=iy-(high-radius);
        if(dx&&dy) {
            dist=dx*dx+dy*dy;
            if(dist>(radius+5)*(radius+5))continue;
            edge=(INTN)radius*radius-dist;
            if(edge<-(radius*8)) {
                p->r=red;p->g=green;p->b=blue;p->a=selected?18:7;
                continue;
            }
            if(edge<0) {p->r=red;p->g=green;p->b=blue;p->a=selected?145:60;continue;}
            if(edge<radius*3) {p->r=red;p->g=green;p->b=blue;p->a=selected?210:130;continue;}
        } else {
            if(ix<low-5 || ix>high+5 || iy<low-5 || iy>high+5)
                continue;
            if(ix<low || ix>high || iy<low || iy>high) {
                p->r=red;p->g=green;p->b=blue;p->a=selected?22:5;continue;
            }
            if(ix<=low+2 || ix>=high-2 || iy<=low+2 || iy>=high-2) {
                p->r=red;p->g=green;p->b=blue;p->a=selected?235:115;continue;
            }
        }
        p->r=9;p->g=16;p->b=21;p->a=selected?48:75;
    }
    return image;
}

EG_IMAGE *BootThemeSelection(UINTN size) {
    return makeThemePlate(size,TRUE);
}
EG_IMAGE *BootThemeTile(UINTN size) {
    return makeThemePlate(size,FALSE);
}

