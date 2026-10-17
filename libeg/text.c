/*
 * libeg/text.c
 * Text drawing functions
 *
 * Copyright (c) 2006 Christoph Pfisterer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Christoph Pfisterer nor the names of the
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "libegint.h"
#include "../refind/global.h"

#include "egemb_font.h"
#include "egemb_font_large.h"
#define FONT_NUM_CHARS 96
#define EXTERNAL_FONT_ALPHA_THRESHOLD 16

static EG_IMAGE *BaseFontImage = NULL;
static EG_IMAGE *DarkFontImage = NULL;
static EG_IMAGE *LightFontImage = NULL;

static UINTN FontCellWidth = 7;
static BOOLEAN FontMetricsReady = FALSE;
static BOOLEAN ProportionalFont = FALSE;
static UINTN FontGlyphLeft[FONT_NUM_CHARS];
static UINTN FontGlyphWidth[FONT_NUM_CHARS];
static UINTN FontGlyphAdvance[FONT_NUM_CHARS];

//
// Text rendering
//

static UINTN egGlyphIndex(IN CHAR16 Character) {
    if ((Character < 32) || (Character >= 127))
        return 95;
    return Character - 32;
} // UINTN egGlyphIndex()

// The historical embedded firmware font is fixed-width. Boot's externally
// generated MB Corpo atlas uses the same 96-cell bitmap contract so it remains
// compatible with the decoder, but the typeface itself is proportional. Derive
// useful per-glyph metrics from the alpha channel once after loading instead of
// forcing every glyph into the widest cell and producing artificial tracking.
static VOID egPrepareFontMetrics(VOID) {
    UINTN Glyph, X, Y, CellStart, Left, Right, Width, Advance;
    BOOLEAN HaveInk;
    EG_PIXEL *Pixel;

    if ((BaseFontImage == NULL) || (BaseFontImage->Width == 0) ||
        (BaseFontImage->Height == 0))
        return;

    FontCellWidth = BaseFontImage->Width / FONT_NUM_CHARS;
    if ((FontCellWidth == 0) || ((BaseFontImage->Width % FONT_NUM_CHARS) != 0)) {
        ProportionalFont = FALSE;
        FontMetricsReady = TRUE;
        return;
    }

    for (Glyph = 0; Glyph < FONT_NUM_CHARS; Glyph++) {
        FontGlyphLeft[Glyph] = 0;
        FontGlyphWidth[Glyph] = FontCellWidth;
        FontGlyphAdvance[Glyph] = FontCellWidth;

        if (!ProportionalFont)
            continue;

        CellStart = Glyph * FontCellWidth;
        Left = FontCellWidth;
        Right = 0;
        HaveInk = FALSE;

        for (Y = 0; Y < BaseFontImage->Height; Y++) {
            for (X = 0; X < FontCellWidth; X++) {
                Pixel = &(BaseFontImage->PixelData[Y * BaseFontImage->Width + CellStart + X]);
                if (Pixel->a > EXTERNAL_FONT_ALPHA_THRESHOLD) {
                    if (!HaveInk || (X < Left))
                        Left = X;
                    if (!HaveInk || (X > Right))
                        Right = X;
                    HaveInk = TRUE;
                }
            }
        }

        if (!HaveInk) {
            // Space has no ink. Give it a restrained proportional advance.
            Advance = (FontCellWidth * 2) / 5;
            if (Advance < 4)
                Advance = 4;
            FontGlyphLeft[Glyph] = 0;
            FontGlyphWidth[Glyph] = 0;
            FontGlyphAdvance[Glyph] = Advance;
            continue;
        }

        Width = Right - Left + 1;
        Advance = Width + 3;
        if (Advance > FontCellWidth)
            Advance = FontCellWidth;

        FontGlyphLeft[Glyph] = Left;
        FontGlyphWidth[Glyph] = Width;
        FontGlyphAdvance[Glyph] = Advance;
    }

    FontMetricsReady = TRUE;
} // VOID egPrepareFontMetrics()

static VOID egPrepareFont() {
    UINTN UGAWidth, UGAHeight;

    egGetScreenSize(&UGAWidth, &UGAHeight);
    if (BaseFontImage == NULL) {
        if (UGAWidth >= HIDPI_MIN)
            BaseFontImage = egPrepareEmbeddedImage(&egemb_font_large, TRUE);
        else
            BaseFontImage = egPrepareEmbeddedImage(&egemb_font, TRUE);
        ProportionalFont = FALSE;
        FontMetricsReady = FALSE;
    }
    if ((BaseFontImage != NULL) && !FontMetricsReady)
        egPrepareFontMetrics();
} // VOID egPrepareFont();

UINTN egGetFontHeight(VOID) {
   egPrepareFont();
   return (BaseFontImage != NULL) ? BaseFontImage->Height : 0;
} // UINTN egGetFontHeight()

UINTN egGetFontCellWidth(VOID) {
   egPrepareFont();
   return FontCellWidth;
}

UINTN egComputeTextWidth(IN CHAR16 *Text) {
   UINTN Width = 0;
   UINTN i;

   egPrepareFont();
   if (Text == NULL)
      return 0;

   if (!ProportionalFont)
      return FontCellWidth * StrLen(Text);

   for (i = 0; i < StrLen(Text); i++)
      Width += FontGlyphAdvance[egGlyphIndex(Text[i])];
   return Width;
} // UINTN egComputeTextWidth()

VOID egMeasureText(IN CHAR16 *Text, OUT UINTN *Width, OUT UINTN *Height) {
    egPrepareFont();

    if (Width != NULL)
        *Width = egComputeTextWidth(Text);
    if (Height != NULL)
        *Height = (BaseFontImage != NULL) ? BaseFontImage->Height : 0;
}

VOID egRenderText(IN CHAR16 *Text, IN OUT EG_IMAGE *CompImage, IN UINTN PosX, IN UINTN PosY, IN UINT8 BGBrightness)
{
    EG_IMAGE        *FontImage;
    EG_PIXEL        *BufferPtr;
    EG_PIXEL        *FontPixelData;
    UINTN           BufferLineOffset, FontLineOffset;
    UINTN           TextLength;
    UINTN           i, c, GlyphWidth, GlyphLeft, Advance, PenX;

    egPrepareFont();
    if ((BaseFontImage == NULL) || (CompImage == NULL) || (Text == NULL) ||
        (PosY >= CompImage->Height) || (PosX >= CompImage->Width))
        return;

    TextLength = StrLen(Text);

    if (BGBrightness < 128) {
       if (LightFontImage == NULL) {
          LightFontImage = egCopyImage(BaseFontImage);
          if (LightFontImage == NULL)
             return;
          for (i = 0; i < (LightFontImage->Width * LightFontImage->Height); i++) {
             LightFontImage->PixelData[i].r = 255 - LightFontImage->PixelData[i].r;
             LightFontImage->PixelData[i].g = 255 - LightFontImage->PixelData[i].g;
             LightFontImage->PixelData[i].b = 255 - LightFontImage->PixelData[i].b;
          } // for
       } // if
       FontImage = LightFontImage;
    } else {
       if (DarkFontImage == NULL)
          DarkFontImage = egCopyImage(BaseFontImage);
       if (DarkFontImage == NULL)
          return;
       FontImage = DarkFontImage;
    } // if/else

    BufferPtr = CompImage->PixelData;
    BufferLineOffset = CompImage->Width;
    BufferPtr += PosY * BufferLineOffset;
    FontPixelData = FontImage->PixelData;
    FontLineOffset = FontImage->Width;
    PenX = PosX;

    for (i = 0; i < TextLength; i++) {
        c = egGlyphIndex(Text[i]);
        GlyphLeft = ProportionalFont ? FontGlyphLeft[c] : 0;
        GlyphWidth = ProportionalFont ? FontGlyphWidth[c] : FontCellWidth;
        Advance = ProportionalFont ? FontGlyphAdvance[c] : FontCellWidth;

        if (PenX >= CompImage->Width)
            break;

        if (GlyphWidth > 0) {
            if ((PenX + GlyphWidth) > CompImage->Width)
                break;
            egRawCompose(BufferPtr + PenX,
                         FontPixelData + c * FontCellWidth + GlyphLeft,
                         GlyphWidth, FontImage->Height,
                         BufferLineOffset, FontLineOffset);
        }
        PenX += Advance;
    }
}

// Load a font bitmap from the specified file. External Boot font atlases are
// proportional faces carried in a fixed-cell compatibility bitmap, so derive
// proportional metrics from their alpha channel after loading.
VOID egLoadFont(IN CHAR16 *Filename) {
   if (DarkFontImage) {
      egFreeImage(DarkFontImage);
      DarkFontImage = NULL;
   }
   if (LightFontImage) {
      egFreeImage(LightFontImage);
      LightFontImage = NULL;
   }
   if (BaseFontImage) {
      egFreeImage(BaseFontImage);
      BaseFontImage = NULL;
   }

   ProportionalFont = TRUE;
   FontMetricsReady = FALSE;
   BaseFontImage = egLoadImage(SelfDir, Filename, TRUE);
   if (BaseFontImage == NULL) {
      Print(L"Note: Font image file %s is invalid! Using default font!\n", Filename);
      ProportionalFont = FALSE;
   }
   egPrepareFont();
} // VOID egLoadFont()

/* EOF */
