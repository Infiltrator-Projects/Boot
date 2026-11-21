# SPDX-License-Identifier: GPL-3.0-or-later
# BOOT: native GNU-EFI x86-64 firmware executable.
# Standard EFI filesystem drivers are built alongside BOOT; other interfaces can coexist.
# Firmware and disk tools are never installed by a build or clean target.
include Make.common

.PHONY: all gnuefi clean
all: gnuefi

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
