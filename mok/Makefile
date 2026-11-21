# Builds BOOT's Secure Boot/shim compatibility support. The presence of
# these objects does not itself establish image authenticity or a fully
# validated platform trust-chain.

SRCDIR = .

VPATH = $(SRCDIR)

LOCAL_GNUEFI_CFLAGS  = -I$(SRCDIR) -I$(SRCDIR)/../include
# The archive links shim verification, firmware policy hooks and the file
# adapter those hooks use. The running boot manager installs the hooks
# conditionally after querying firmware state.
OBJS            = guid.o mok.o security_policy.o simple_file.o
TARGET          = libmok.a

all: $(TARGET)

include $(SRCDIR)/../Make.common

clean:
	rm -f $(TARGET) *~ *.so $(OBJS) *.efi *.obj boot_*.txt boot_*.dll *.lib
