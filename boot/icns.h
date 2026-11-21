/*
 * Embedded image and OS icon resource contract. Icon lookup may return a
 * fallback/dummy image and must not imply that its corresponding EFI image
 * was validated, authenticated or executed successfully.
 */
 


































 








#ifndef __ICNS_H_
#define __ICNS_H_

 
 
 

EG_IMAGE * LoadOSIcon(IN CHAR16 *OSIconName OPTIONAL, IN CHAR16 *FallbackIconName, BOOLEAN BootLogo);

EG_IMAGE * DummyImage(IN UINTN PixelSize);

EG_IMAGE * BuiltinIcon(IN UINTN Id);

#define BUILTIN_ICON_FUNC_ABOUT            (0)
#define BUILTIN_ICON_FUNC_RESET            (1)
#define BUILTIN_ICON_FUNC_SHUTDOWN         (2)
#define BUILTIN_ICON_FUNC_EXIT             (3)
#define BUILTIN_ICON_FUNC_FIRMWARE         (4)
#define BUILTIN_ICON_FUNC_CSR_ROTATE       (5)
#define BUILTIN_ICON_FUNC_HIDDEN           (6)
#define BUILTIN_ICON_FUNC_INSTALL          (7)
#define BUILTIN_ICON_FUNC_BOOTORDER        (8)
#define BUILTIN_ICON_TOOL_SHELL            (9)
#define BUILTIN_ICON_TOOL_PART             (10)
#define BUILTIN_ICON_TOOL_RESCUE           (11)
#define BUILTIN_ICON_TOOL_APPLE_RESCUE     (12)
#define BUILTIN_ICON_TOOL_WINDOWS_RESCUE   (13)
#define BUILTIN_ICON_TOOL_MOK_TOOL         (14)
#define BUILTIN_ICON_TOOL_FWUPDATE         (15)
#define BUILTIN_ICON_TOOL_MEMTEST          (16)
#define BUILTIN_ICON_TOOL_NETBOOT          (17)
#define BUILTIN_ICON_VOL_INTERNAL          (18)
#define BUILTIN_ICON_VOL_EXTERNAL          (19)
#define BUILTIN_ICON_VOL_OPTICAL           (20)
#define BUILTIN_ICON_VOL_NET               (21)
#define BUILTIN_ICON_VOL_EFI               (22)
#define BUILTIN_ICON_MOUSE                 (23)
#define BUILTIN_ICON_COUNT                 (24)

#endif

 
