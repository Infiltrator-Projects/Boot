









SRCDIR = .

VPATH = $(SRCDIR)

LOCAL_GNUEFI_CFLAGS  = -I$(SRCDIR) -I$(SRCDIR)/../include

OBJS            = nanojpeg.o nanojpeg_xtra.o screen.o image.o text.o load_bmp.o load_icns.o lodepng.o lodepng_xtra.o
TARGET          = libeg.a

all: $(TARGET)

include $(SRCDIR)/../Make.common

clean:
	rm -f $(TARGET) *~ *.so $(OBJS) *.efi *.obj boot_*.txt boot_*.dll *.lib


