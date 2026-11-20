BOOT — Infiltrator-Projects UEFI boot manager
=============================================

Source: https://github.com/Infiltrator-Projects/Boot
Licence: GPL-3.0-or-later for the BOOT-derived firmware code, subject to
the preserved copyright and licence notices in individual components.

Architecture
------------
The x86-64 BOOT EFI executable resides at:
  /EFI/InfiltratorOS/BOOT/BOOTX64.EFI
on the selected FAT EFI System Partition. It does not take ownership of
/EFI/BOOT, /EFI/Microsoft/Boot, or GRUB directories.

The native .run embeds the actual boot-install shell installation logic.
It downloads the pinned BOOT source revision, compiles locally using GNU-EFI
and CPU-native options, then deploys BOOT and registers an EFI firmware
entry. The .run has no build-only mode. The generic .deb stages firmware
assets in /usr/share/infiltrator-boot; it does not deploy them to the ESP.

Filesystem architecture
-----------------------
Only Common V2 filesystem modules are an acceptable final architecture.
The migration is NOT complete. Legacy EFI driver loading remains in the
current firmware for compatibility; it is not evidence of Common V2 support.
Do not treat firmware or Secure Boot integration as qualified on real
hardware simply because GitHub Actions builds the executable.

Development
-----------
See BUILDING.txt for exact build, test and release commands.
The active release scripts are packaging/build-release.sh and
packaging/test-release.sh. Historical upstream packaging and dangerous
relocation helpers are not part of this repository.

Provenance
----------
BOOT derives from the earlier rEFInd project and its predecessors, with
third-party UEFI, image-decoding and compression components. Preserve
copyright and applicable third-party licence notices when distributing
modified binaries. Refer to the preserved notices in the source files.
