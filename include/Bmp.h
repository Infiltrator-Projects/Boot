 













#ifndef _BMP_H__
#define _BMP_H__

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
