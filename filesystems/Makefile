# Selects filesystem driver build targets. The normal GNU-EFI release
# requires these six drivers; NTFS remains an explicit optional target.
# Installation belongs to the native installer and generic package, not
# the driver build.
FILESYSTEMS = ext2 ext4 reiserfs iso9660 hfs btrfs
DRIVERS = $(FILESYSTEMS) ntfs
FILESYSTEMS_GNUEFI = $(addsuffix _gnuefi,$(FILESYSTEMS))
DRIVERS_GNUEFI = $(addsuffix _gnuefi,$(DRIVERS))
TEXTFILES = $(FILESYSTEMS:=*.txt)

.PHONY: all gnuefi all_gnuefi clean $(DRIVERS) $(DRIVERS_GNUEFI)
all: $(FILESYSTEMS)
gnuefi all_gnuefi: $(FILESYSTEMS_GNUEFI)

# FSTYPE changes the shared bridge's compiled protocol table. Rebuild it
# before each backend and serialize the targets so parallel make cannot
# link another filesystem's table. Explicit phony targets also prevent
# a same-named file from suppressing a required driver build.
.NOTPARALLEL:
$(DRIVERS): %:
	rm -f fsw_efi.obj
	+$(MAKE) DRIVERNAME=$* -f Make.tiano

$(DRIVERS_GNUEFI): %_gnuefi:
	rm -f fsw_efi.o
	+$(MAKE) DRIVERNAME=$* -f Make.gnuefi

clean:
	rm -f *~ *.bak *.o *.obj *.so *.efi *.dll err.txt $(TEXTFILES)
	+$(MAKE) -C test clean
