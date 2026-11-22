/*
 * BOOT's UEFI-native graphical theme.
 *
 * The background is a native JPEG asset loaded by the existing firmware
 * decoder. BOOT never needs an image converter, GPU, filesystem driver,
 * Linux framebuffer or network access to show it. The logo overlays two cyan
 * parallelograms with EXACT geometry from the canonical two-bar Infiltrator mark
 * and draws the genuine MB Corpo A Title face at a native rasterisation size.
 *
 * All images allocated here are owned by this module or returned to a caller
 * that must release them; the cached background is borrowed and never passed
 * to egFreeImage by a caller. Resolution changes invalidate only that cache.
 */
#include "global.h"
#include "screen.h"
#include "theme.h"
#include "log.h"

static EG_IMAGE *CachedBackground;
static UINTN CachedWidth, CachedHeight;

EG_IMAGE *BootThemeBackground(UINTN width, UINTN height) {
    EG_IMAGE *loaded, *resized;
    if (!width || !height || width>8192 || height>8192)
        return NULL;
    if (CachedBackground && width==CachedWidth && height==CachedHeight)
        return CachedBackground;
    loaded=egLoadImage(SelfDir,L"themes\\boot-luxury.jpg",FALSE);
    if (!loaded) {
        LOG(1,LOG_LINE_NORMAL,L"Graphite BOOT wallpaper absent; using legacy graphics");
        return NULL;
    }
    resized=egScaleImage(loaded,width,height);
    egFreeImage(loaded);
    if (!resized) return NULL;
    egFreeImage(CachedBackground);
    CachedBackground=resized;
    CachedWidth=width;
    CachedHeight=height;
    LOG(1,LOG_LINE_NORMAL,L"BOOT luxury graphite background loaded at %dx%d",width,height);
    return CachedBackground;
}

/* Emulates the two canonical polygon paths precisely at arbitrary DPI:
 * left slash (19,7)-(33,7)-(19,41)-(5,41); second is offset +26.
 * Four subsamples per pixel give edge coverage without a firmware SVG parser.
 */
static VOID drawTwinSlash(EG_IMAGE *image,UINTN x0,UINTN y0,
                          UINTN width,UINTN height) {
    UINTN x,y;
    if (!image || !width || !height) return;
    for(y=0;y<height;++y) {
        UINTN py=y0+y;
        if (py>=image->Height) break;
        for(x=0;x<width;++x) {
            UINTN px=x0+x;
            UINTN sy,sx,covered=0;
            EG_PIXEL *p;
            if(px>=image->Width)break;
            for(sy=0;sy<2;++sy)for(sx=0;sx<2;++sx) {
                /* Fixed-point SVG coordinates x,y scaled by 256. */
                INTN xx=(INTN)(((x*4+sx*2+1)*64*256)/(width*4));
                INTN yy=(INTN)(((y*4+sy*2+1)*48*256)/(height*4));
                INTN lead,relative;
                if(yy<7*256 || yy>=41*256)continue;
                lead=19*256-(yy-7*256)*14/34;
                relative=xx-lead;
                if((relative>=0&&relative<14*256)||
                   (relative>=26*256&&relative<40*256))
                    ++covered;
            }
            if(!covered)continue;
            p=&image->PixelData[py*image->Width+px];
            p->r=39;
            p->g=220;
            p->b=233;
            p->a=(UINT8)((covered*255)/4);
        }
    }
}

/* Restore the original icon canvas before every menu repaint. There are no
 * labels, fixed OS names or faked controls baked into the wallpaper. BOOT's
 * menu still owns the live OS icons, selection, shortcuts and help strings. */
UINTN BootThemeDrawWordmark(EG_IMAGE *background) {
    EG_IMAGE *letters, *region;
    EG_PIXEL clear={0,0,0,0};
    UINTN size,wordWidth,slashWidth,slashHeight,gap,plateWidth,plateHeight;
    UINTN left,top,x,y;
    if(!background || background->Width<400 || background->Height<300)
        return 0;
    size=background->Width/16;
    if(size>116) size=116;
    if(size<44) size=44;
    wordWidth=egComputeTextWidthAtPixelHeight(L"BOOT",MB_FONT_TITLE,size);
    if (!wordWidth || wordWidth>background->Width/2)return 0;
    slashHeight=(size*75)/100;
    slashWidth=(slashHeight*4)/3;
    gap=size/10;
    plateWidth=slashWidth+gap+wordWidth+12;
    plateHeight=size+18;
    if(plateWidth>background->Width || plateHeight>background->Height)
        return 0;
    left=(background->Width-plateWidth)/2;
    top=(background->Height*20)/100;
    if(top+plateHeight>background->Height)return 0;
    letters=egCreateFilledImage(plateWidth,plateHeight,TRUE,&clear);
    if(!letters)return 0;
    drawTwinSlash(letters,0,(plateHeight-slashHeight)/2,slashWidth,slashHeight);
    egRenderTextAtPixelHeight(L"BOOT",letters,slashWidth+gap,2,0,
                              MB_FONT_TITLE,size);
    /* Silver-white face with controlled metallic shading: retain the glyph's
     * antialiased alpha from the actual TrueType outlines. Twin cyan bars are
     * not recoloured by this pass. */
    for(y=0;y<plateHeight;++y)for(x=slashWidth+gap;x<plateWidth;++x) {
        EG_PIXEL *p=&letters->PixelData[y*letters->Width+x];
        UINTN shade;
        if(!p->a)continue;
        shade=(y<plateHeight/3)?245:(y<plateHeight*2/3?214:233);
        p->r=(UINT8)shade;
        p->g=(UINT8)(shade+((245-shade)/3));
        p->b=(UINT8)(shade+((255-shade)/2));
    }
    region=egCropImage(background,left,top,plateWidth,plateHeight);
    if(region) {
        egComposeImage(region,letters,0,0);
        egDrawImage(region,left,top);
        egFreeImage(region);
        LOG(1,LOG_LINE_NORMAL,L"BOOT twin-cyan-slash MB Corpo wordmark rasterised");
    }
    egFreeImage(letters);
    return top+plateHeight;
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

/* Material-neutral tool icon projection. The original loaded artwork remains
 * untouched; this is a disposable copy with its alpha and anti-aliasing kept.
 * Cyan is reserved for selected controls and reboot, silver for other tools.
 * A non-alpha icon is returned unchanged to prevent tinting opaque artwork. */
EG_IMAGE *BootThemeTintToolIcon(EG_IMAGE *original, BOOLEAN accent) {
    EG_IMAGE *copy;
    UINTN i, count;
    if(!original || !original->HasAlpha)return NULL;
    copy=egCopyImage(original);
    if(!copy)return NULL;
    count=copy->Width*copy->Height;
    for(i=0;i<count;++i) {
        EG_PIXEL *p=&copy->PixelData[i];
        UINTN luminance,scale;
        if(!p->a)continue;
        luminance=(UINTN)p->r*3+(UINTN)p->g*6+(UINTN)p->b;
        luminance/=10;
        if(luminance<16) {
            p->a=(UINT8)((UINTN)p->a*luminance/32);
            continue;
        }
        scale=145+luminance/3;
        if(scale>240)scale=240;
        if(accent) {
            p->r=(UINT8)(39*scale/240);
            p->g=(UINT8)(220*scale/240);
            p->b=(UINT8)(233*scale/240);
        } else {
            p->r=(UINT8)(216*scale/240);
            p->g=(UINT8)(222*scale/240);
            p->b=(UINT8)(229*scale/240);
        }
    }
    return copy;
}
