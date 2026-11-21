/*
 * Private graphical image/decoder implementation contract. Pixel sizes,
 * alpha channels, palette conversions and pooled allocation must preserve
 * bounds across PNG, BMP, ICNS and JPEG readers.
 */
 



































#ifndef __LIBEG_LIBEGINT_H__
#define __LIBEG_LIBEGINT_H__

#ifdef __MAKEWITH_GNUEFI
#include <efi.h>
#include <efilib.h>
#else
#include "../include/tiano_includes.h"
#endif

#include "libeg.h"

 

typedef EG_IMAGE * (*EG_DECODE_FUNC)(IN UINT8 *FileData, IN UINTN FileDataLength, IN UINTN IconSize, IN BOOLEAN WantAlpha);

 

BOOLEAN egSetScreenSize(IN OUT UINTN *ScreenWidth, IN OUT UINTN *ScreenHeight);

VOID egRestrictImageArea(IN EG_IMAGE *Image,
                         IN UINTN AreaPosX, IN UINTN AreaPosY,
                         IN OUT UINTN *AreaWidth, IN OUT UINTN *AreaHeight);
VOID egRawCopy(IN OUT EG_PIXEL *CompBasePtr, IN EG_PIXEL *TopBasePtr,
               IN UINTN Width, IN UINTN Height,
               IN UINTN CompLineOffset, IN UINTN TopLineOffset);
VOID egRawCompose(IN OUT EG_PIXEL *CompBasePtr, IN EG_PIXEL *TopBasePtr,
                  IN UINTN Width, IN UINTN Height,
                  IN UINTN CompLineOffset, IN UINTN TopLineOffset);
/*
 * Form a byte pointer to one component of the first pixel. The plane helpers
 * advance by sizeof(EG_PIXEL) to visit the same component in successive
 * interleaved pixels.
 */
#define PLPTR(imagevar, colorname) ((UINT8 *) &((imagevar)->PixelData->colorname))

VOID egDecompressIcnsRLE(IN OUT UINT8 **CompData, IN OUT UINTN *CompLen, IN UINT8 *DestPlanePtr, IN UINTN PixelCount);
VOID egInsertPlane(IN UINT8 *SrcDataPtr, IN UINT8 *DestPlanePtr, IN UINTN PixelCount);
VOID egSetPlane(IN UINT8 *DestPlanePtr, IN UINT8 Value, IN UINTN PixelCount);
VOID egCopyPlane(IN UINT8 *SrcPlanePtr, IN UINT8 *DestPlanePtr, IN UINTN PixelCount);

EG_IMAGE * egDecodeBMP(IN UINT8 *FileData, IN UINTN FileDataLength, IN UINTN IconSize, IN BOOLEAN WantAlpha);
EG_IMAGE * egDecodeICNS(IN UINT8 *FileData, IN UINTN FileDataLength, IN UINTN IconSize, IN BOOLEAN WantAlpha);

VOID egEncodeBMP(IN EG_IMAGE *Image, OUT UINT8 **FileData, OUT UINTN *FileDataLength);


#endif  

 
