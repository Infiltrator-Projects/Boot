 






















#ifndef __UGA_DRAW_H__
#define __UGA_DRAW_H__

#define EFI_UGA_DRAW_PROTOCOL_GUID \
{ \
    0x982c298b, 0xf4fa, 0x41cb, { 0xb8, 0x38, 0x77, 0xaa, 0x68, 0x8f, 0xb8, 0x39 } \
}

 
struct _EFI_UGA_DRAW_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *EFI_UGA_DRAW_PROTOCOL_GET_MODE) (
                                          IN  struct _EFI_UGA_DRAW_PROTOCOL * This,
                                          OUT UINT32                *HorizontalResolution,
                                          OUT UINT32                *VerticalResolution,
                                          OUT UINT32                *ColorDepth,
                                          OUT UINT32                *RefreshRate
                                          )
 

















;

typedef
EFI_STATUS
(EFIAPI *EFI_UGA_DRAW_PROTOCOL_SET_MODE) (
                                          IN  struct _EFI_UGA_DRAW_PROTOCOL * This,
                                          IN  UINT32                HorizontalResolution,
                                          IN  UINT32                VerticalResolution,
                                          IN  UINT32                ColorDepth,
                                          IN  UINT32                RefreshRate
                                          )
 
















;

typedef struct {
    UINT8 Blue;
    UINT8 Green;
    UINT8 Red;
    UINT8 Reserved;
} EFI_UGA_PIXEL;

typedef union {
    EFI_UGA_PIXEL Pixel;
    UINT32        Raw;
} EFI_UGA_PIXEL_UNION;

typedef enum {
    EfiUgaVideoFill,
    EfiUgaVideoToBltBuffer,
    EfiUgaBltBufferToVideo,
    EfiUgaVideoToVideo,
    EfiUgaBltMax
} EFI_UGA_BLT_OPERATION;

typedef
EFI_STATUS
(EFIAPI *EFI_UGA_DRAW_PROTOCOL_BLT) (
                                     IN  struct _EFI_UGA_DRAW_PROTOCOL           * This,
                                     IN  EFI_UGA_PIXEL                           * BltBuffer, OPTIONAL
                                     IN  EFI_UGA_BLT_OPERATION                   BltOperation,
                                     IN  UINTN                                   SourceX,
                                     IN  UINTN                                   SourceY,
                                     IN  UINTN                                   DestinationX,
                                     IN  UINTN                                   DestinationY,
                                     IN  UINTN                                   Width,
                                     IN  UINTN                                   Height,
                                     IN  UINTN                                   Delta         OPTIONAL
                                     );

 












































;

typedef struct _EFI_UGA_DRAW_PROTOCOL {
    EFI_UGA_DRAW_PROTOCOL_GET_MODE  GetMode;
    EFI_UGA_DRAW_PROTOCOL_SET_MODE  SetMode;
    EFI_UGA_DRAW_PROTOCOL_BLT       Blt;
} EFI_UGA_DRAW_PROTOCOL;

extern EFI_GUID gEfiUgaDrawProtocolGuid;

#endif
