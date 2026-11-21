# Compiles the bundled DEFLATE decoder used by BOOT compressed-load paths.
# Linker, allocation and ABI settings must match EFI code, not a hosted zlib.










SRCDIR = .

VPATH = $(SRCDIR)

LOCAL_GNUEFI_CFLAGS  = -I$(SRCDIR) -I$(SRCDIR)/../include
# decompress_inflate.c includes the DEFLATE table builder, fast loop and state
# machine directly. Listing those sources again here would link their
# definitions twice.
OBJS            = decompress_inflate.o
TARGET          = libgzip.a

all: $(TARGET)

include $(SRCDIR)/../Make.common

clean:
	rm -f $(TARGET) *~ *.so $(OBJS) *.efi *.obj boot_*.txt boot_*.dll *.lib


