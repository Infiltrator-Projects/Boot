# Compiles EFI compatibility/driver-model helpers using the common
# firmware ABI profile. Linking success alone is not proof that a particular
# UEFI vendor exposes every expected controller protocol.









SRCDIR = .

VPATH = $(SRCDIR)

LOCAL_GNUEFI_CFLAGS  = -I$(SRCDIR) -I$(SRCDIR)/../include

OBJS            = gnuefi-helper.o legacy.o BdsHelper.o BdsTianoCore.o
TARGET          = libEfiLib.a

all: $(TARGET)

include $(SRCDIR)/../Make.common

clean:
	rm -f *.o *.obj *~ *.lib *.a
