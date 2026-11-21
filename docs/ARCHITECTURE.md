# Architecture

## Purpose and execution boundary

BOOT is the UEFI boot manager owned by InfiltratorOS and intended to launch **other supported operating systems as well as InfiltratorOS**. It is a firmware-stage application, not an installed operating-system kernel, Linux VFS implementation or replacement for every other firmware boot entry. It must not assume a fixed list of installed systems.

The present executable is `boot/boot_x64.efi`, packaged and installed as `/EFI/InfiltratorOS/BOOT/BOOTX64.EFI`. The current main AMD64 build uses GNU-EFI. Some source files still contain conditional code and alternate firmware build infrastructure; their presence does not establish independently qualified releases for those architectures.

```text
UEFI firmware / firmware BootOrder
          |
          v
/EFI/InfiltratorOS/BOOT/BOOTX64.EFI
          |
          +-- BOOT configuration and EFI services
          +-- EFI filesystem-driver loading
          +-- Volume and bootloader discovery
          +-- Operating-system / loader selection menu
          |
          +-- EFI image or EFI-stub kernel handoff
                  |
                  v
          Selected operating system
```

## Runtime component ownership

| Source | Responsibility |
| --- | --- |
| `boot/main.c` | BOOT lifecycle, initialisation, interface orchestration and main event loop |
| `boot/config.c`, `boot/config.h` | Configuration-file parsing and runtime selection settings |
| `boot/scan.c`, `boot/scan.h` | Volume/loader discovery, default icon choice, fallback handling and menu entries |
| `boot/menu.c`, `boot/screen.c`, `boot/pointer.c` | Firmware UI, selection, graphics/text display and user input |
| `boot/launch_efi.c` | EFI image validation/launch path and filesystem-driver connection during EFI handoff |
| `boot/linux.c` | Linux EFI-stub kernel-specific boot discovery and option handling |
| `boot/driver_support.c` | EFI driver binding and controller connection support |
| `boot/install.c` | BOOT installation-related firmware mechanisms within the UEFI program |
| `boot/lib.c`, `boot/mystrings.c` | UEFI-facing utilities and string/path handling |
| `libeg/`, `EfiLib/` | Graphics handling and EFI support routines |
| `mok/` | Existing Secure Boot-related code and interfaces |
| `filesystems/` | The six primary built EFI filesystem drivers and additional source/build targets |
| `boot-native-installer.run` | Single native build-and-install path for the versioned `.run` release |

The current codebase originated from the established rEFInd implementation. It retains its discovery, menu and EFI filesystem mechanisms; no documentation should claim that those mechanisms were replaced by a new universal boot engine during the recent cleanup.

## Volume discovery and filesystem-driver contract

UEFI firmware exposes device handles and protocols. BOOT performs firmware-stage discovery and uses EFI filesystem drivers to make additional volumes readable. The GNU-EFI make target builds the EFI application and the EXT2, EXT4, ReiserFS, ISO 9660, HFS-family and Btrfs drivers as separately built `*_x64.efi` files. The native installer requires each of these drivers and installs them beneath BOOT's own `drivers_x64/` folder.

This is the **current deployed filesystem integration**. It must not be conflated with the intended Infiltrator Common V2 filesystem interface. A Common V2 `.so`, a hosted filesystem driver, and a freestanding EFI firmware driver are different output types. Shared source or ABI compatibility cannot be inferred from identical filesystem names. The future V2 architecture requires explicit binary format, platform ABI, dependency, security, loading and read-path proofs; it is not declared completed here.

A loader is discoverable only if BOOT can reach its volume, identify a candidate and accept the candidate as a loader. A recognized filesystem signature is not a guarantee that its contents can be read. Similarly, discovery of a kernel file does not by itself prove its command line, initrd, EFI-stub compatibility or successful operating-system start.

## Boot selection and preservation of alternatives

The configured operating-system selection is a **presentation policy** on top of general discovery, not a set of hard-coded OS choices.

The currently maintained defaults in `boot.conf-sample` are:

```ini
timeout 20
use_nvram false
dont_scan_files +,grubx64.efi,EFI/BOOT/bootx64.efi
default_selection vmlinuz
```

The default file also contains disabled example menu stanzas. Their presence does not create active fixed menu entries. The `+` syntax for `dont_scan_files` retains existing exclusions while adding GRUB's usual filename and the EFI fallback path. Hiding those from the BOOT UI does **not** delete those executables or their independent firmware entries. On a given machine the firmware's one-time boot selector may still expose them.

`default_selection vmlinuz` prefers matching direct-kernel entries; it is not an assertion that every system has a working EFI-stub kernel. Other operating-system discoveries remain available. BOOT should be able to show every supported installation on systems with numerous volumes or operating systems. Display filtering is separate from firmware-level recoverability.

`use_nvram false` is a BOOT runtime configuration setting; it is **not** a prohibition on the native installer using UEFI NVRAM to register and prioritise the BOOT firmware entry.

## Installation and ownership boundaries

The published native `.run` is a single, complete installation script. It elevates when necessary, verifies a source revision pinned by the release, builds BOOT and the six drivers, and invokes its own existing install flow. It has no permitted alternate ESP destination and no build-only execution mode.

The destination path is strictly `/EFI/InfiltratorOS/BOOT`, and the native executable's filename is `BOOTX64.EFI`. The install guards check the destination directory, symbolic-link hazards and a SHA-256 marker (`.infiltrator-boot-loader.sha256`) against the existing BOOT executable before replacing it. On a Linux-host installation, a verified reinstall removes and recreates BOOT's dedicated directory, so **existing BOOT configuration and assets in that directory are not preserved**. The Windows, GRUB and standard `EFI/BOOT` paths remain outside this deletion scope.

The installer calls its firmware-entry management routines to establish BOOT as the preferred firmware choice. The tests exercise detection of duplicate/stale entries, preservation of non-BOOT entries and repeated installation ordering; those are distinct from the BOOT runtime menu's choices. Failure to identify ownership or firmware state correctly must not be papered over with a second BOOT entry.

The generic AMD64 `.deb` does not execute these installation operations. Its responsibility is staging the firmware image, drivers, configuration sample and other resources under `/usr/share/infiltrator-boot`. Packaging it is not equivalent to installing a UEFI boot manager.

## Build and release ownership

`Makefile` calls the GNU-EFI build for `libeg/`, `mok/`, `gzip/`, `EfiLib/`, `boot/` and `filesystems/`. `packaging/build-release.sh` pins the exact source commit in the downloadable `.run` and creates a staged `.deb` plus checksums. `packaging/test-release.sh` verifies artifact metadata, expected driver contents and native-install structure.

`.github/workflows/boot.yml` owns current AMD64 verification and automated prerelease publication. The source tree also includes older or alternative make targets; only the explicit native release path is authoritative for the shipped `.run`.

## Graphical presentation and product identity

The menu code can load supplied icons, banners, fonts, selection images and other resources. Those are lower-level capabilities of the present firmware renderer, not approval for a user-installable theme ecosystem. BOOT is intended to carry one official InfiltratorOS visual design. The optional Snowy assets have been removed from the repository; the basic icon/graphics infrastructure remains.

Appearance changes must not alter scan ordering, delete other loaders, disable filesystem drivers or change the native installer. A missing image cannot be allowed to invalidate an otherwise sound boot path.

## Integrity and trust boundaries

There are distinct integrity boundaries: the downloaded source must match the pinned release commit; an existing BOOT binary must match its install ownership marker before replacement; EFI images and drivers are handled through firmware execution and security mechanisms; and Secure Boot acceptance depends on the firmware's active trust configuration. A SHA-256 ownership marker is **not** equivalent to Secure Boot authentication.

Input surfaces include ESP directory entries, filesystem metadata, configuration text, EFI load options, firmware NVRAM state and optional hardware-dependent paths. Malformed or missing data must not be represented as successful OS discovery, authenticated execution or a verified firmware update.

## Unverified architectural objectives

The desired Common V2-only filesystem module design, universal operating-system compatibility, production-grade security qualification and complete custom InfiltratorOS appearance require explicit future evidence. This document records the implemented BOOT revision and does not silently declare those objectives done. See [Hardware](HARDWARE.md) and [Validation](VALIDATION.md) for observed limits.
