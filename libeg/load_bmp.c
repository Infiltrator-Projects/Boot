/*
 * BOOT / Bitmap decoding
 *
 * BMP resource decoding and encoding.
 *
 * Parses packed headers and padded scanlines before producing EFI pixels.
 * Header lengths, dimensions and data offsets require bounds checks.
 */

#include "libegint.h"

#ifdef __MAKEWITH_GNUEFI
#pragma pack(1)

typedef struct {
    UINT8   Blue;
    UINT8   Green;
    UINT8   Red;
    UINT8   Reserved;
} BMP_COLOR_MAP;

typedef struct {
    CHAR8         CharB;
    CHAR8         CharM;
    UINT32        Size;
    UINT16        Reserved[2];
    UINT32        ImageOffset;
    UINT32        HeaderSize;
    UINT32        PixelWidth;
    UINT32        PixelHeight;
    UINT16        Planes;        
    UINT16        BitPerPixel;   
    UINT32        CompressionType;
    UINT32        ImageSize;     
    UINT32        XPixelsPerMeter;
    UINT32        YPixelsPerMeter;
    UINT32        NumberOfColors;
    UINT32        ImportantColors;
} BMP_IMAGE_HEADER;

#pragma pack()
#endif

/*
 * Validates BMP layout and converts packed pixel data to an EG_IMAGE.
 * A malformed row stride or short image must not access beyond file bytes.
 */
EG_IMAGE * egDecodeBMP(IN UINT8 *FileData, IN UINTN FileDataLength, IN UINTN IconSize, IN BOOLEAN WantAlpha)
{
    EG_IMAGE            *NewImage;
    BMP_IMAGE_HEADER    *BmpHeader;
    BMP_COLOR_MAP       *BmpColorMap;
    UINTN               x, y;
    UINT8               *ImagePtr;
    UINT8               *ImagePtrBase;
    UINTN               ImageLineOffset;
    UINT8               ImageValue = 0, AlphaValue;
    EG_PIXEL            *PixelPtr;
    UINTN               Index, BitIndex;

    if (FileDataLength < sizeof(BMP_IMAGE_HEADER) || FileData == NULL)
        return NULL;
    BmpHeader = (BMP_IMAGE_HEADER *) FileData;
    if (BmpHeader->CharB != 'B' || BmpHeader->CharM != 'M')
        return NULL;
    if (BmpHeader->CompressionType != 0)
        return NULL;
    if (BmpHeader->BitPerPixel != 1 && BmpHeader->BitPerPixel != 4 &&
        BmpHeader->BitPerPixel != 8 && BmpHeader->BitPerPixel != 24)
        return NULL;
    /*
     * Convert pixels to packed row bytes for the selected depth, then round
     * up to a four-byte BMP row boundary. Padding affects the source stride
     * and file-length check. The destination stores one full EG_PIXEL per
     * pixel.
     */
    ImageLineOffset = BmpHeader->PixelWidth;
    if (BmpHeader->BitPerPixel == 24)
        ImageLineOffset *= 3;
    else if (BmpHeader->BitPerPixel == 1)
        ImageLineOffset = (ImageLineOffset + 7) >> 3;
    else if (BmpHeader->BitPerPixel == 4)
        ImageLineOffset = (ImageLineOffset + 1) >> 1;
    if ((ImageLineOffset % 4) != 0)
        ImageLineOffset = ImageLineOffset + (4 - (ImageLineOffset % 4));

    if (BmpHeader->ImageOffset + ImageLineOffset * BmpHeader->PixelHeight > FileDataLength)
        return NULL;

    NewImage = egCreateImage(BmpHeader->PixelWidth, BmpHeader->PixelHeight, WantAlpha);
    if (NewImage == NULL)
        return NULL;
    AlphaValue = WantAlpha ? 255 : 0;

    BmpColorMap = (BMP_COLOR_MAP *)(FileData + sizeof(BMP_IMAGE_HEADER));
    ImagePtrBase = FileData + BmpHeader->ImageOffset;
    for (y = 0; y < BmpHeader->PixelHeight; y++) {
        ImagePtr = ImagePtrBase;
        ImagePtrBase += ImageLineOffset;
        PixelPtr = NewImage->PixelData + (BmpHeader->PixelHeight - 1 - y) * BmpHeader->PixelWidth;

        switch (BmpHeader->BitPerPixel) {

            case 1:
                for (x = 0; x < BmpHeader->PixelWidth; x++) {
                    BitIndex = x & 0x07;
                    if (BitIndex == 0)
                        ImageValue = *ImagePtr++;

                    Index = (ImageValue >> (7 - BitIndex)) & 0x01;
                    PixelPtr->b = BmpColorMap[Index].Blue;
                    PixelPtr->g = BmpColorMap[Index].Green;
                    PixelPtr->r = BmpColorMap[Index].Red;
                    PixelPtr->a = AlphaValue;
                    PixelPtr++;
                }
                break;

            case 4:
                for (x = 0; x <= BmpHeader->PixelWidth - 2; x += 2) {
                    ImageValue = *ImagePtr++;

                    Index = ImageValue >> 4;
                    PixelPtr->b = BmpColorMap[Index].Blue;
                    PixelPtr->g = BmpColorMap[Index].Green;
                    PixelPtr->r = BmpColorMap[Index].Red;
                    PixelPtr->a = AlphaValue;
                    PixelPtr++;

                    Index = ImageValue & 0x0f;
                    PixelPtr->b = BmpColorMap[Index].Blue;
                    PixelPtr->g = BmpColorMap[Index].Green;
                    PixelPtr->r = BmpColorMap[Index].Red;
                    PixelPtr->a = AlphaValue;
                    PixelPtr++;
                }
                if (x < BmpHeader->PixelWidth) {
                    ImageValue = *ImagePtr++;

                    Index = ImageValue >> 4;
                    PixelPtr->b = BmpColorMap[Index].Blue;
                    PixelPtr->g = BmpColorMap[Index].Green;
                    PixelPtr->r = BmpColorMap[Index].Red;
                    PixelPtr->a = AlphaValue;
                    PixelPtr++;
                }
                break;

            case 8:
                for (x = 0; x < BmpHeader->PixelWidth; x++) {
                    Index = *ImagePtr++;
                    PixelPtr->b = BmpColorMap[Index].Blue;
                    PixelPtr->g = BmpColorMap[Index].Green;
                    PixelPtr->r = BmpColorMap[Index].Red;
                    PixelPtr->a = AlphaValue;
                    PixelPtr++;
                }
                break;

            case 24:
                for (x = 0; x < BmpHeader->PixelWidth; x++) {
                    PixelPtr->b = *ImagePtr++;
                    PixelPtr->g = *ImagePtr++;
                    PixelPtr->r = *ImagePtr++;
                    PixelPtr->a = AlphaValue;
                    PixelPtr++;
                }
                break;

        }
    }

    return NewImage;
}

/**
 * Write 24-bit BGR rows from the bottom of the image upwards. Round every output
 * row to a multiple of four bytes. A three-pixel row therefore occupies twelve
 * bytes, including three zero padding bytes supplied by AllocateZeroPool. The
 * returned file allocation belongs to the caller.
 */
VOID egEncodeBMP(IN EG_IMAGE *Image, OUT UINT8 **FileDataReturn, OUT UINTN *FileDataLengthReturn)
{
    BMP_IMAGE_HEADER    *BmpHeader;
    UINT8               *FileData;
    UINTN               FileDataLength;
    UINT8               *ImagePtr;
    UINT8               *ImagePtrBase;
    UINTN               ImageLineOffset;
    EG_PIXEL            *PixelPtr;
    UINTN               x, y;

    ImageLineOffset = Image->Width * 3;
    if ((ImageLineOffset % 4) != 0)
        ImageLineOffset = ImageLineOffset + (4 - (ImageLineOffset % 4));

    FileDataLength = sizeof(BMP_IMAGE_HEADER) + Image->Height * ImageLineOffset;
    FileData = AllocateZeroPool(FileDataLength);
    if (FileData == NULL) {
        Print(L"Error allocate %d bytes\n", FileDataLength);
        *FileDataReturn = NULL;
        *FileDataLengthReturn = 0;
        return;
    }

    BmpHeader = (BMP_IMAGE_HEADER *)FileData;
    BmpHeader->CharB = 'B';
    BmpHeader->CharM = 'M';
    BmpHeader->Size = FileDataLength;
    BmpHeader->ImageOffset = sizeof(BMP_IMAGE_HEADER);
    BmpHeader->HeaderSize = 40;
    BmpHeader->PixelWidth = Image->Width;
    BmpHeader->PixelHeight = Image->Height;
    BmpHeader->Planes = 1;
    BmpHeader->BitPerPixel = 24;
    BmpHeader->CompressionType = 0;
    BmpHeader->XPixelsPerMeter = 0xb13;
    BmpHeader->YPixelsPerMeter = 0xb13;

    ImagePtrBase = FileData + BmpHeader->ImageOffset;
    for (y = 0; y < Image->Height; y++) {
        ImagePtr = ImagePtrBase;
        ImagePtrBase += ImageLineOffset;
        PixelPtr = Image->PixelData + (Image->Height - 1 - y) * Image->Width;

        for (x = 0; x < Image->Width; x++) {
            *ImagePtr++ = PixelPtr->b;
            *ImagePtr++ = PixelPtr->g;
            *ImagePtr++ = PixelPtr->r;
            PixelPtr++;
        }
    }

    *FileDataReturn = FileData;
    *FileDataLengthReturn = FileDataLength;
}
