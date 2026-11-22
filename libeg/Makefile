# Builds firmware graphics, font and image-format support. UI resources
# are optional for EFI loader detection and must not become hard boot gates.

SRCDIR = .

VPATH = $(SRCDIR)

LOCAL_GNUEFI_CFLAGS  = -I$(SRCDIR) -I$(SRCDIR)/../include
# Keep the PNG and JPEG decoder objects paired with their _xtra adapters. The
# adapters supply firmware allocation and convert decoder output into BOOT's
# EG_IMAGE layout.
OBJS            = nanojpeg.o nanojpeg_xtra.o screen.o image.o ttf.o load_bmp.o load_icns.o lodepng.o lodepng_xtra.o
TARGET          = libeg.a

all: $(TARGET)

include $(SRCDIR)/../Make.common

clean:
	rm -f $(TARGET) *~ *.so $(OBJS) *.efi *.obj boot_*.txt boot_*.dll *.lib


# The TrueType rasteriser performs floating-point glyph geometry. The
# firmware's remaining source files retain the native general-register-only
# profile; only this object opts into x86-64 SSE2, without a hosted libm.
ifeq ($(ARCH),x86_64)
MB_TTF_FPFLAGS = -msse2 -mfpmath=sse
endif
ttf.o: ttf.c stb_truetype.h
	$(CC) $(CFLAGS) $(ARCH_CFLAGS) $(GNUEFI_CFLAGS) $(LOCAL_GNUEFI_CFLAGS) \
	    $(MB_TTF_FPFLAGS) -ffreestanding -fno-builtin -D__MAKEWITH_GNUEFI -c $< -o $@
