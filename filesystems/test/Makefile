# Builds POSIX-hosted filesystem exercisers. These tests exercise FSW
# parsing against host-backed images but do not install EFI protocols or
# qualify BOOT execution on real firmware.

DRIVERNAME = xfs

CC		= /usr/bin/gcc
CFLAGS		= -Wall -g -D_REENTRANT -DVERSION=\"$(VERSION)\" -DHOST_POSIX -I ../ -DFSTYPE=$(DRIVERNAME)

FSW_NAMES       = ../fsw_core ../fsw_lib
FSW_OBJS	= $(FSW_NAMES:=.o)
LSLR_OBJS	= $(FSW_OBJS) ../fsw_$(DRIVERNAME).o fsw_posix.o lslr.o
LSLR_BIN	= lslr
# lslr substitutes DRIVERNAME into its backend object. The separate lsroot
# list below hard-codes XFS and a dot-prefixed POSIX object, so changing
# DRIVERNAME alone does not retarget lsroot.
LSROOT_OBJS	= $(FSW_OBJS) ../fsw_xfs.o .fsw_posix.o lsroot.o
LSROOT_BIN	= lsroot

$(LSLR_BIN):	$(LSLR_OBJS)
		$(CC) $(CFLAGS) -o $(LSLR_BIN) $(LSLR_OBJS) $(LDFLAGS)

$(LSROOT_BIN):	$(LSROOT_OBJS) 
		$(CC) $(CFLAGS) -o $(LSROOT_BIN) $(LSROOT_OBJS) $(LDFLAGS)

all:		$(LSLR_BIN) $(LSROOT_BIN)

clean:		
		@rm -f *.o ../*.o lslr lsroot
