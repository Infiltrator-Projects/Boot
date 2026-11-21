 










 














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
