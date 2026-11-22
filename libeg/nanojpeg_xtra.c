/*
 * BOOT / JPEG decoder integration
 *
 * NanoJPEG-to-EFI image conversion.
 *
 * Decoder pixels remain owned by NanoJPEG until njDone(). Both RGB and
 * grayscale inputs are copied into caller-owned images.
 */

#include "global.h"
#include "../boot/screen.h"
#include "../boot/lib.h"

#define _NJ_INCLUDE_HEADER_ONLY
#include "nanojpeg.c"

/*
 * Decoder storage is borrowed only until the pixel copy finishes. Failure
 * always exits through njDone(); the returned EG_IMAGE, if any, is owned by
 * the caller. IconSize is intentionally unused: resizing is handled by the
 * normal image loader after decoding.
 */
EG_IMAGE *egDecodeJPEG(IN UINT8 *FileData, IN UINTN FileDataLength,
                       IN UINTN IconSize, IN BOOLEAN WantAlpha) {
    EG_IMAGE *image = NULL;
    const UINT8 *decoded;
    UINTN count, i, channels;
    unsigned width, height;

    (void)IconSize;
    if (!FileData || FileDataLength < 2 || FileDataLength > 0x7fffffffU)
        return NULL;
    if (!njInit())
        return NULL;

    if (njDecode((const void *)FileData, (int)FileDataLength) != NJ_OK)
        goto done;

    width = (unsigned)njGetWidth();
    height = (unsigned)njGetHeight();
    channels = njIsColor() ? 3 : 1;

    /* Bound allocation, multiplication and the NanoJPEG-reported plane size. */
    if (width == 0 || height == 0 || width > 8192 || height > 8192)
        goto done;
    count = (UINTN)width * (UINTN)height;
    if (njGetImageSize() <= 0 ||
        (UINTN)njGetImageSize() != count * channels)
        goto done;

    decoded = (const UINT8 *)njGetImage();
    if (!decoded)
        goto done;

    image = egCreateImage(width, height, WantAlpha);
    if (!image)
        goto done;

    for (i = 0; i < count; ++i) {
        UINT8 red, green, blue;
        if (channels == 3) {
            red = decoded[i * 3];
            green = decoded[i * 3 + 1];
            blue = decoded[i * 3 + 2];
        } else {
            red = green = blue = decoded[i];
        }
        image->PixelData[i].r = red;
        image->PixelData[i].g = green;
        image->PixelData[i].b = blue;
        image->PixelData[i].a = WantAlpha ? 255 : 0;
    }

done:
    /* NanoJPEG still owns decoded; do not free it separately. */
    njDone();
    return image;
}
