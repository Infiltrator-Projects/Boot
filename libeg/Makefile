# Builds firmware graphics, font and image-format support. UI resources
# are optional for EFI loader detection and must not become hard boot gates.

SRCDIR = .

VPATH = $(SRCDIR)

LOCAL_GNUEFI_CFLAGS  = -I$(SRCDIR) -I$(SRCDIR)/../include
# Keep the PNG and JPEG decoder objects paired with their _xtra adapters. The
# adapters supply firmware allocation and convert decoder output into BOOT's
# EG_IMAGE layout.
OBJS            = nanojpeg.o nanojpeg_xtra.o screen.o image.o text.o load_bmp.o load_icns.o lodepng.o lodepng_xtra.o
TARGET          = libeg.a

all: $(TARGET)

include $(SRCDIR)/../Make.common

clean:
	rm -f $(TARGET) *~ *.so $(OBJS) *.efi *.obj boot_*.txt boot_*.dll *.lib
