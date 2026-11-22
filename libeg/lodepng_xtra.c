/*
 * Firmware allocation wrappers and PNG-to-EG_IMAGE adapter. Standard PNG
 * library memory callbacks are replaced with EFI-compatible allocation.
 * A valid decode must still satisfy requested image size and alpha semantics.
 * Ownership is not fully symmetrical in this adapter: the reallocator keeps
 * the previous pool allocation and several early exits omit decoder-buffer
 * cleanup. Do not interpret successful compilation as allocator hygiene.
 */

#include "global.h"
#include "../boot/screen.h"
#include "../boot/lib.h"
#include "lodepng.h"

/*
 * Prefix every decoded allocation with its requested payload size, returning
 * a pointer past the prefix. size + sizeof(size_t) is not overflow-checked;
 * a huge decoder-requested size can wrap before AllocateZeroPool. The prefix
 * is private to this allocator family and must be recovered at free time.
 */
void* lodepng_malloc(size_t size) {
   void *ptr;

   ptr = AllocateZeroPool(size + sizeof(size_t));
   if (ptr) {
      *(size_t *) ptr = size;
      return ((size_t *) ptr) + 1;
   } else {
      return NULL;
   }
}  

void lodepng_free (void *ptr) {
   if (ptr) {
      ptr = (void *) (((size_t *) ptr) - 1);
      MyFreePool(ptr);
   }
}  

static size_t report_size(void *ptr) {
   if (ptr)
      return * (((size_t *) ptr) - 1);
   else
      return 0;
}  

/**
 * Recover the old byte count from the prefix maintained by lodepng_malloc.
 * Allocate the new region and copy the smaller of the two sizes. The old
 * allocation is still live after a successful call because this implementation
 * has no release of ptr.
 */
void* lodepng_realloc(void *ptr, size_t new_size) {
   size_t *new_pool;
   size_t old_size;

   new_pool = lodepng_malloc(new_size);
   if (new_pool && ptr) {
      /*
       * The shim prefixes each pool allocation with its byte length. Copy at
       * most the smaller payload into the new block. This implementation
       * returns the replacement without freeing ptr.
       */
      old_size = report_size(ptr);
      MyCopyMem(new_pool, ptr, (old_size < new_size) ? old_size : new_size);
   }
   return new_pool;
}  

int MyStrlen(const char *InString) {
   int Length = 0;

   if (InString) {
      while (InString[Length] != '\0')
         Length++;
   }
   return Length;
}  

VOID *MyMemSet(VOID *s, int c, size_t n) {
    SetMem(s, c, n);
    return s;
}

VOID *MyMemCpy(void *__restrict __dest, const void *__restrict __src, size_t __n) {
    MyCopyMem(__dest, __src, __n);
    return __dest;
}

typedef struct _lode_color {
   UINT8 red;
   UINT8 green;
   UINT8 blue;
   UINT8 alpha;
} lode_color;

/**
 * Request eight-bit RGBA pixels from LodePNG and copy channels into the EFI pixel
 * structure by name. WantAlpha controls whether the destination alpha byte is
 * populated. Image dimensions come from the decoded PNG, and icon resizing occurs
 * later in the loading path. The decoder owns PixelData until lodepng_free;
 * the current early returns after successful decoding omit that release
 * when EG_IMAGE creation or dimension validation fails. This can leak pool
 * allocations on malformed-resource and allocation-failure paths.
 */
EG_IMAGE * egDecodePNG(IN UINT8 *FileData, IN UINTN FileDataLength, IN UINTN IconSize, IN BOOLEAN WantAlpha) {
   EG_IMAGE *NewImage = NULL;
   unsigned Error, Width, Height;
   EG_PIXEL *PixelData;
   lode_color *LodeData;
   UINTN i;

   Error = lodepng_decode_memory((unsigned char **) &PixelData, &Width, &Height, (unsigned char*) FileData,
                                 (size_t) FileDataLength, LCT_RGBA, 8);

   if (Error) {
      return NULL;
   }

   NewImage = egCreateImage(Width, Height, WantAlpha);
   if (NewImage == NULL)
       return NULL;

   if ((NewImage->Width != Width) || (NewImage->Height != Height)) {
      egFreeImage(NewImage);
      return NULL;
   }

   /*
    * The decoder was asked for eight-bit RGBA, while EG_PIXEL stores colour
    * components in BGR order. Assign the named fields explicitly and retain
    * source alpha only for an alpha-bearing surface.
    */
   LodeData = (lode_color *) PixelData;

   for (i = 0; i < (NewImage->Height * NewImage->Width); i++) {
      NewImage->PixelData[i].r = LodeData[i].red;
      NewImage->PixelData[i].g = LodeData[i].green;
      NewImage->PixelData[i].b = LodeData[i].blue;
      if (WantAlpha)
         NewImage->PixelData[i].a = LodeData[i].alpha;
   }
   lodepng_free(PixelData);

   return NewImage;
}  
