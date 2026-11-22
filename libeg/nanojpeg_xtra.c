/*
 * NanoJPEG-to-EG_IMAGE adaptor for BOOT graphics. JPEG data is optional
 * resource content, never a prerequisite for scanning or launching an EFI
 * image. Adapter allocations are independent of loader and firmware handles.
 * The decoded colour buffer belongs to NanoJPEG and njDone() frees it.
 * This adapter also frees that pointer before calling njDone(), risking a
 * double free. Grayscale decoding uses a single byte per pixel but this
 * adapter assumes interleaved RGB triples without testing njIsColor().
 */

#include "global.h"
#include "../boot/screen.h"
#include "../boot/lib.h"

#define _NJ_INCLUDE_HEADER_ONLY
#include "nanojpeg.c"

typedef struct _jpeg_color {
   UINT8 red;
   UINT8 green;
   UINT8 blue;
} jpeg_color;

/**
 * Allocate at the dimensions returned by NanoJPEG and copy its packed colour
 * values into EG_PIXEL members. Requested alpha is initialised to fully opaque.
 * IconSize does not scale this image here. The normal success path releases the
 * decoded buffer after the copy, then njDone() attempts to free that same
 * decoder-owned buffer again. This is not valid ownership transfer.
 * The failure path after njDecode does not explicitly tear down the context.
 */
EG_IMAGE * egDecodeJPEG(IN UINT8 *FileData, IN UINTN FileDataLength, IN UINTN IconSize, IN BOOLEAN WantAlpha) {
    EG_IMAGE *NewImage = NULL;
    unsigned Width, Height;
    jpeg_color *JpegData;
    UINTN i;
    nj_result_t Result;

    if (njInit()) {
        Result = njDecode((VOID *) FileData, FileDataLength);
        if (Result != NJ_OK)
            return NULL;

        Width = njGetWidth();
        Height = njGetHeight();

        NewImage = egCreateImage(Width, Height, WantAlpha);
        if ((NewImage == NULL) || (NewImage->Width != Width) || (NewImage->Height != Height))
            return NULL;
        /*
         * This loop consumes an RGB triple per pixel and supplies an opaque
         * alpha channel on request. The decoder returns a one-byte plane for
         * grayscale images, so this path assumes a three-component decoder
         * result.
         */
        JpegData = (jpeg_color *) njGetImage();

        for (i = 0; i < (NewImage->Height * NewImage->Width); i++) {
            NewImage->PixelData[i].r = JpegData[i].red;
            NewImage->PixelData[i].g = JpegData[i].green;
            NewImage->PixelData[i].b = JpegData[i].blue;

            if (WantAlpha)
                NewImage->PixelData[i].a = 255;
        }
        MyFreePool(JpegData);
        njDone();
    }

    return NewImage;
}  
