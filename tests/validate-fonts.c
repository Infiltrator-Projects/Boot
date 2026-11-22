/*
 * Hosted qualification of the exact TrueType outline parser used by BOOT.
 * Each of the 23 canonical files must initialise and produce actual nonempty
 * grayscale glyph bitmaps at both standard and HiDPI menu sizes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "../libeg/stb_truetype.h"

static int test_face(const char *path) {
    FILE *in = fopen(path, "rb");
    long size;
    unsigned char *bytes;
    stbtt_fontinfo font;
    int px, ci;
    const int codepoints[] = { 'A', 'g', '0', '?', 0x00E9 };
    if (!in) { fprintf(stderr,"Cannot open: %s\n",path); return 0; }
    if (fseek(in,0,SEEK_END)!=0 || (size=ftell(in))<64 || size>2097152L ||
        fseek(in,0,SEEK_SET)!=0) { fclose(in); return 0; }
    bytes=malloc((size_t)size);
    if (!bytes || fread(bytes,1,(size_t)size,in)!=(size_t)size) {
        free(bytes); fclose(in); return 0;
    }
    fclose(in);
    if (!stbtt_InitFont(&font,bytes,stbtt_GetFontOffsetForIndex(bytes,0))) {
        fprintf(stderr,"Unparseable TTF: %s\n",path); free(bytes); return 0;
    }
    for (px=18; px<=30; px+=12) {
        float scale=stbtt_ScaleForPixelHeight(&font,(float)px);
        if (!(scale>0.0f)) { free(bytes); return 0; }
        for (ci=0; ci<(int)(sizeof(codepoints)/sizeof(codepoints[0])); ++ci) {
            int cp=codepoints[ci],w=0,h=0,xoff=0,yoff=0,advance=0;
            unsigned char *bitmap;
            size_t i,n;
            int painted=0;
            if (!stbtt_FindGlyphIndex(&font,cp)) {
                /* Some specialty fonts omit accented glyphs; fallback must
                 * remain renderable for the firmware's unsupported codepoints. */
                cp='?';
            }
            stbtt_GetCodepointHMetrics(&font,cp,&advance,NULL);
            if (advance<=0) { fprintf(stderr,"Nonpositive glyph advance: %s\n",path);free(bytes);return 0; }
            bitmap=stbtt_GetCodepointBitmap(&font,0,scale,cp,&w,&h,&xoff,&yoff);
            if (!bitmap || w<=0 || h<=0 || w>256 || h>256) {
                fprintf(stderr,"Raster failure: %s at %dpx U+%04x\n",path,px,cp);
                stbtt_FreeBitmap(bitmap,NULL); free(bytes);return 0;
            }
            n=(size_t)w*(size_t)h;
            for (i=0;i<n;++i) if (bitmap[i]) { painted=1; break; }
            stbtt_FreeBitmap(bitmap,NULL);
            if (!painted) { fprintf(stderr,"Blank glyph: %s\n",path);free(bytes);return 0; }
        }
    }
    printf("TTF parsed and rasterised: %s\n",path);
    free(bytes); return 1;
}
int main(int argc,char **argv) {
    int i;
    if (argc!=24) { fprintf(stderr,"Expected exactly 23 TTF paths, got %d\n",argc-1); return 2; }
    for (i=1;i<argc;++i) if (!test_face(argv[i])) return 1;
    puts("All 23 font faces passed parse, spacing and glyph bitmap tests at 18 and 30 pixels.");
    return 0;
}
