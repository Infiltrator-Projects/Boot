








SRCDIR = .

VPATH = $(SRCDIR)

LOCAL_GNUEFI_CFLAGS  = -I$(SRCDIR) -I$(SRCDIR)/../include

OBJS            = gnuefi-helper.o legacy.o BdsHelper.o BdsTianoCore.o
TARGET          = libEfiLib.a

all: $(TARGET)

include $(SRCDIR)/../Make.common

clean:
	rm -f *.o *.obj *~ *.lib *.a
