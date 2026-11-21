/*
 * Packed BMP palette/header records used by EFI image decoding. Bitmap
 * header sizes, scanline strides and pixel offsets must be checked against
 * the provided file length before any cast to these on-disk structures.
 */

#ifndef _BMP_H__
#define _BMP_H__
/*
 * BMP offsets start at the first file byte. Packing keeps the 14-byte file
 * header and following bitmap information at their serialized offsets when
 * this layout is overlaid on an input buffer.
 */
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
