/*
 * Icon acquisition and fallback for firmware-visible operating-system entries.
 * Images come from BOOT resources or embedded artwork, not from a proof that
 * a loader is executable. A missing icon must not suppress an otherwise valid
 * candidate: dummy imagery is an explicit graphics fallback.
 */
 



































#include "global.h"
#include "lib.h"
#include "icns.h"
#include "log.h"
#include "config.h"
#include "mystrings.h"
#include "../boot/screen.h"

 
 
 

typedef struct {
    EG_IMAGE    *Image;
    CHAR16      *FileName;
    UINTN       IconSize;
} BUILTIN_ICON;

BUILTIN_ICON BuiltinIconTable[BUILTIN_ICON_COUNT] = {
   { NULL, L"func_about", ICON_SIZE_SMALL },
   { NULL, L"func_reset", ICON_SIZE_SMALL },
   { NULL, L"func_shutdown", ICON_SIZE_SMALL },
   { NULL, L"func_exit", ICON_SIZE_SMALL },
   { NULL, L"func_firmware", ICON_SIZE_SMALL },
   { NULL, L"func_csr_rotate", ICON_SIZE_SMALL },
   { NULL, L"func_hidden", ICON_SIZE_SMALL },
   { NULL, L"func_install", ICON_SIZE_SMALL },
   { NULL, L"func_bootorder", ICON_SIZE_SMALL },
   { NULL, L"tool_shell", ICON_SIZE_SMALL },
   { NULL, L"tool_part", ICON_SIZE_SMALL },
   { NULL, L"tool_rescue", ICON_SIZE_SMALL },
   { NULL, L"tool_apple_rescue", ICON_SIZE_SMALL },
   { NULL, L"tool_windows_rescue", ICON_SIZE_SMALL },
   { NULL, L"tool_mok_tool", ICON_SIZE_SMALL },
   { NULL, L"tool_fwupdate", ICON_SIZE_SMALL },
   { NULL, L"tool_memtest", ICON_SIZE_SMALL },
   { NULL, L"tool_netboot", ICON_SIZE_SMALL },
   { NULL, L"vol_internal", ICON_SIZE_BADGE },
   { NULL, L"vol_external", ICON_SIZE_BADGE },
   { NULL, L"vol_optical", ICON_SIZE_BADGE },
   { NULL, L"vol_net", ICON_SIZE_BADGE },
   { NULL, L"vol_efi", ICON_SIZE_BADGE },
   { NULL, L"mouse", ICON_SIZE_MOUSE }
};

/*
 * Caches an embedded icon after its first decode. Returned images belong to
 * the icon cache; consumers must not assume exclusive ownership.
 */
/*
 * Resolves a numeric builtin-icon identifier to cached image resources.
 * Image availability concerns the presentation layer and is independent of
 * whether the corresponding EFI loader can be opened or authenticated.
 */
EG_IMAGE * BuiltinIcon(IN UINTN Id)
{
    if (Id >= BUILTIN_ICON_COUNT)
        return NULL;

    if (BuiltinIconTable[Id].Image == NULL) {
       BuiltinIconTable[Id].Image = egFindIcon(BuiltinIconTable[Id].FileName, GlobalConfig.IconSizes[BuiltinIconTable[Id].IconSize]);
       if (BuiltinIconTable[Id].Image == NULL)
          BuiltinIconTable[Id].Image = DummyImage(GlobalConfig.IconSizes[BuiltinIconTable[Id].IconSize]);
    }  

    return BuiltinIconTable[Id].Image;
}

 
 
 

 
 
 
 
/*
 * Tries configured OS-name candidates, a fallback icon and a boot-logo
 * alternative before returning a dummy. The order is presentation policy.
 */
/*
 * Looks up operating-system artwork with an explicit fallback candidate.
 * The chosen icon is a visual hint, not a filesystem probe or boot result.
 */
EG_IMAGE * LoadOSIcon(IN CHAR16 *OSIconName OPTIONAL, IN CHAR16 *FallbackIconName, BOOLEAN BootLogo)
{
    EG_IMAGE        *Image = NULL;
    CHAR16          *CutoutName, *BaseName;
    UINTN           Index = 0;

    LOG(4, LOG_LINE_NORMAL, L"Entering LoadOSIcon()");
    if (GlobalConfig.TextOnly)       
        return NULL;

    LOG(4, LOG_LINE_NORMAL, L"Trying to find an icon from '%s'", OSIconName);
     
    while (((CutoutName = FindCommaDelimited(OSIconName, Index++)) != NULL) && (Image == NULL)) {
       BaseName = PoolPrint(L"%s_%s", BootLogo ? L"boot" : L"os", CutoutName);
       Image = egFindIcon(BaseName, GlobalConfig.IconSizes[ICON_SIZE_BIG]);
       MyFreePool(CutoutName);
       MyFreePool(BaseName);
    }

     
    if (Image == NULL) {
       BaseName = PoolPrint(L"%s_%s", BootLogo ? L"boot" : L"os", FallbackIconName);
       LOG(4, LOG_LINE_NORMAL, L"Trying to find an icon from '%s'", BaseName);
       Image = egFindIcon(BaseName, GlobalConfig.IconSizes[ICON_SIZE_BIG]);
       MyFreePool(BaseName);
    }

     
    if (BootLogo && (Image == NULL)) {
       BaseName = PoolPrint(L"os_%s", FallbackIconName);
       LOG(4, LOG_LINE_NORMAL, L"Trying to find an icon from '%s'", BaseName);
       Image = egFindIcon(BaseName, GlobalConfig.IconSizes[ICON_SIZE_BIG]);
       MyFreePool(BaseName);
    }

     
    if (Image == NULL) {
       LOG(4, LOG_LINE_NORMAL, L"Setting dummy image");
       Image = DummyImage(GlobalConfig.IconSizes[ICON_SIZE_BIG]);
    }

    return Image;
}  


static EG_PIXEL BlackPixel  = { 0x00, 0x00, 0x00, 0 };
 

/*
 * Constructs placeholder graphics when requested imagery is unavailable;
 * using this result must not change the selected loader path.
 */
EG_IMAGE * DummyImage(IN UINTN PixelSize)
{
    EG_IMAGE        *Image;
    UINTN           x, y, LineOffset;
    CHAR8           *Ptr, *YPtr;

    Image = egCreateFilledImage(PixelSize, PixelSize, TRUE, &BlackPixel);

    LineOffset = PixelSize * 4;

    YPtr = (CHAR8 *)Image->PixelData + ((PixelSize - 32) >> 1) * (LineOffset + 4);
    for (y = 0; y < 32; y++) {
        Ptr = YPtr;
        for (x = 0; x < 32; x++) {
            if (((x + y) % 12) < 6) {
                *Ptr++ = 0;
                *Ptr++ = 0;
                *Ptr++ = 0;
            } else {
                *Ptr++ = 0;
                *Ptr++ = 255;
                *Ptr++ = 255;
            }
            *Ptr++ = 144;
        }
        YPtr += LineOffset;
    }

    return Image;
}
