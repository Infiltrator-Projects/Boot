/*
 * Firmware graphical font loading, glyph measurement and image text draw.
 * Embedded font atlases are resource data; glyph metrics and alpha blending
 * must remain compatible with framebuffer dimensions and configured scale.
 */

#include "libegint.h"
#include "../boot/global.h"

#include "egemb_font.h"
#include "egemb_font_large.h"
#define FONT_NUM_CHARS 96

static EG_IMAGE *BaseFontImage = NULL;
static EG_IMAGE *DarkFontImage = NULL;
static EG_IMAGE *LightFontImage = NULL;

static UINTN FontCellWidth = 7;

/**
 * Select the embedded atlas once, using the initial horizontal resolution to
 * choose the HiDPI asset. Its width divided by FONT_NUM_CHARS gives the fixed
 * glyph advance used by both measurement and painting.
 */
static VOID egPrepareFont() {
    UINTN UGAWidth, UGAHeight;

    egGetScreenSize(&UGAWidth, &UGAHeight);
    if (BaseFontImage == NULL) {
        if (UGAWidth >= HIDPI_MIN)
            BaseFontImage = egPrepareEmbeddedImage(&egemb_font_large, TRUE);
        else
            BaseFontImage = egPrepareEmbeddedImage(&egemb_font, TRUE);
    }
    if (BaseFontImage != NULL)
        FontCellWidth = BaseFontImage->Width / FONT_NUM_CHARS;
}  

/*
 * Demand-load the embedded font before obtaining its pixel height.
 * The code assumes font preparation succeeds and dereferences
 * BaseFontImage without an allocation-failure fallback.
 */
UINTN egGetFontHeight(VOID) {
   egPrepareFont();
   return BaseFontImage->Height;
}  

/*
 * Return the cached fixed-glyph advance in pixels. The cache is
 * populated by egPrepareFont, so a call before preparation may
 * observe its uninitialised/default width.
 */
UINTN egGetFontCellWidth(VOID) {
   return FontCellWidth;
}

/*
 * Calculate the rendered width as a glyph count times the cached
 * fixed-cell advance; this is not a Unicode shaping or kerning
 * measurement. Font preparation precedes the calculation.
 */
UINTN egComputeTextWidth(IN CHAR16 *Text) {
   UINTN Width = 0;

   egPrepareFont();
   if (Text != NULL)
      Width = FontCellWidth * StrLen(Text);
   return Width;
}  

VOID egMeasureText(IN CHAR16 *Text, OUT UINTN *Width, OUT UINTN *Height) {
    egPrepareFont();

    if (Width != NULL)
        *Width = StrLen(Text) * FontCellWidth;
    if (Height != NULL)
        *Height = BaseFontImage->Height;
}

/**
 * The atlas contains fixed-width cells. Printable ASCII maps to indices 0 through
 * 94 and other CHAR16 values select cell 95. The light font is a cached RGB
 * inversion that preserves alpha. The width calculation clips whole glyphs.
 * Callers supply a valid origin and room for the full glyph height.
 */
VOID egRenderText(IN CHAR16 *Text, IN OUT EG_IMAGE *CompImage, IN UINTN PosX, IN UINTN PosY, IN UINT8 BGBrightness)
{
    EG_IMAGE        *FontImage;
    EG_PIXEL        *BufferPtr;
    EG_PIXEL        *FontPixelData;
    UINTN           BufferLineOffset, FontLineOffset;
    UINTN           TextLength;
    UINTN           i, c;

    egPrepareFont();

    if (Text)
       TextLength = StrLen(Text);
    else
       TextLength = 0;

    if (TextLength * FontCellWidth + PosX > CompImage->Width)
        TextLength = (CompImage->Width - PosX) / FontCellWidth;

    if (BGBrightness < 128) {
       if (LightFontImage == NULL) {
          LightFontImage = egCopyImage(BaseFontImage);
          if (LightFontImage == NULL)
             return;
          for (i = 0; i < (LightFontImage->Width * LightFontImage->Height); i++) {
             LightFontImage->PixelData[i].r = 255 - LightFontImage->PixelData[i].r;
             LightFontImage->PixelData[i].g = 255 - LightFontImage->PixelData[i].g;
             LightFontImage->PixelData[i].b = 255 - LightFontImage->PixelData[i].b;
          }  
       }  
       FontImage = LightFontImage;
    } else {
       if (DarkFontImage == NULL)
          DarkFontImage = egCopyImage(BaseFontImage);
       if (DarkFontImage == NULL)
          return;
       FontImage = DarkFontImage;
    }  

    BufferPtr = CompImage->PixelData;
    BufferLineOffset = CompImage->Width;
    BufferPtr += PosX + PosY * BufferLineOffset;
    FontPixelData = FontImage->PixelData;
    FontLineOffset = FontImage->Width;
    for (i = 0; i < TextLength; i++) {
        /*
         * The font atlas stores printable ASCII in consecutive cells starting
         * at space. Unsupported characters share its last cell. Each glyph
         * copy uses the full atlas width as source stride while the
         * destination advances by one cell.
         */
        c = Text[i];
        if (c < 32 || c >= 127)
            c = 95;
        else
            c -= 32;
        egRawCompose(BufferPtr, FontPixelData + c * FontCellWidth,
                     FontCellWidth, FontImage->Height,
                     BufferLineOffset, FontLineOffset);
        BufferPtr += FontCellWidth;
    }
}

/*
 * Reads configured font resource data into graphical rendering state.
 * Missing fonts must not make the firmware boot menu unusable.
 */
VOID egLoadFont(IN CHAR16 *Filename) {
   if (BaseFontImage)
      egFreeImage(BaseFontImage);

   BaseFontImage = egLoadImage(SelfDir, Filename, TRUE);
   if (BaseFontImage == NULL)
      Print(L"Note: Font image file %s is invalid! Using default font!\n");
    egPrepareFont();
}  
