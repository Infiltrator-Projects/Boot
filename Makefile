# Top-level GNU-EFI build graph. The standard gnuefi target delegates
# library and EFI-driver builds to subdirectories; it does not install to
# an ESP or modify NVRAM. Native installation is owned by the .run script.

include Make.common

.PHONY: all gnuefi clean
all: gnuefi
# Build the support archives before linking the application. Filesystem
# drivers are separate firmware images compiled afterwards, so building BOOT
# successfully does not by itself produce the release's driver set.
gnuefi:
	+$(MAKE) MAKEWITH=GNUEFI -C libeg
	+$(MAKE) MAKEWITH=GNUEFI -C mok
	+$(MAKE) MAKEWITH=GNUEFI -C gzip
	+$(MAKE) MAKEWITH=GNUEFI -C EfiLib
	+$(MAKE) MAKEWITH=GNUEFI -C boot
	+$(MAKE) MAKEWITH=GNUEFI REFIND_SBAT_CSV=$(BOOT_SBAT_CSV) -C filesystems all_gnuefi

clean:
	+$(MAKE) MAKEWITH=GNUEFI -C filesystems clean
	+$(MAKE) MAKEWITH=GNUEFI -C boot clean
	+$(MAKE) MAKEWITH=GNUEFI -C EfiLib clean
	+$(MAKE) MAKEWITH=GNUEFI -C gzip clean
	+$(MAKE) MAKEWITH=GNUEFI -C mok clean
	+$(MAKE) MAKEWITH=GNUEFI -C libeg clean
	rm -f include/*~
