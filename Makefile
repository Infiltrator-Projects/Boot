# SPDX-License-Identifier: GPL-3.0-or-later
# BOOT: native GNU-EFI x86-64 firmware executable.
# Filesystem implementations belong to the independent Filesystem-* modules.
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

clean:
	+$(MAKE) MAKEWITH=GNUEFI -C boot clean
	+$(MAKE) MAKEWITH=GNUEFI -C EfiLib clean
	+$(MAKE) MAKEWITH=GNUEFI -C gzip clean
	+$(MAKE) MAKEWITH=GNUEFI -C mok clean
	+$(MAKE) MAKEWITH=GNUEFI -C libeg clean
	rm -f include/*~
