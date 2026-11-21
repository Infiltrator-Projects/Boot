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

UINTN egGetFontHeight(VOID) {
   egPrepareFont();
   return BaseFontImage->Height;
}  

UINTN egGetFontCellWidth(VOID) {
   return FontCellWidth;
}

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

/*
 * Draws a UTF-16 menu string into the destination image with calculated
 * cell metrics. Glyph fallback is visual and never a loader-validity test.
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

 
