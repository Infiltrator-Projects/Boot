# BOOT

BOOT is the UEFI boot manager supplied by **InfiltratorOS**. It is intended to discover and start every supported operating system installed on a computer, not to make InfiltratorOS exclusive and not to remove other operating systems or their firmware boot entries.

BOOT presents detected operating-system and loader choices from the firmware environment. The supported choices depend on the firmware, readable volumes, EFI loaders and kernel boot methods actually present. Discovery of many installations is a requirement; it must not be replaced by a hard-coded Windows/Linux/InfiltratorOS menu.

## Current implementation

The present source is a BOOT-branded UEFI application descended from an established boot-manager implementation built with GNU-EFI. The first-row operating-system menu, loader scanning, EFI launch path, Linux direct-kernel support, filesystem-driver loading and graphical presentation are implemented in `boot/` and associated libraries.

The standard AMD64 build produces `boot/boot_x64.efi` and six EFI filesystem drivers: EXT2, EXT4, ReiserFS, ISO 9660, HFS-family and Btrfs. Drivers are built from `filesystems/` and installed under BOOT's dedicated `drivers_x64/` directory. These are the **current UEFI drivers**; their existence must not be represented as proof that BOOT already loads Common V2 filesystem `.so` modules. The Common V2 transition is an architectural objective requiring separate implementation and validation.

The default configuration in `boot.conf-sample` hides GRUB's `grubx64.efi` and the standard EFI fallback loader from BOOT's graphical list, while leaving those files and their independent firmware boot paths in place. It prefers a directly discovered `vmlinuz` kernel when available. These display rules do not limit scanning to a particular number of installed operating systems.

**Design approach: ELEGANCE.** BOOT aims for the artistic sophistication, balanced composition and meticulous finish of luxury design, not mere minimalism. Its source comments and documentation must be equally well presented without losing technical depth. The governing definition is [Engineering Philosophy](docs/PHILOSOPHY.md#elegance--the-design-approach).

BOOT ships the official InfiltratorOS artwork only. Its PNG/JPEG theme files are selected by `boot.conf` (`theme_background`, `theme_mark`, `theme_underline`, and optional `theme_logo`), not by hardcoded loader filenames. Users can replace those paths with their own PNG artwork or set them to `none`; BOOT can run without image assets. The existing `banner`, `icons_dir`, `selection_big`, `selection_small`, and font directives continue to work; no alternative themes or upstream theme bundle support is shipped.

## Repository layout

| Location | Responsibility |
| --- | --- |
| `boot/` | EFI entry point, discovery, configuration, menus, device/driver integration and EFI loader launch |
| `filesystems/` | Current EFI filesystem driver implementations and build profiles |
| `EfiLib/`, `libeg/`, `gzip/`, `mok/` | Firmware support, graphics, decompression and security-related integration |
| `include/`, `icons/`, `fonts/`, `banners/`, `keys/` | Shared headers and bundled resources |
| `boot-native-installer.run` | Sole native source-build-and-install program for the published AMD64 `.run` |
| `packaging/` | Generic `.deb` installer and source-pinned native `.run` creation/verification |
| `tests/` | Disposable installation, NVRAM and OVMF checks |
| `.github/workflows/boot.yml` | Native build, qualification and release automation |

## Build and release

The GNU-EFI build is invoked with:

```sh
make gnuefi
```

It requires the firmware development toolchain and headers, and the required tools differ between a local build and the release workflow. Building alone does **not** install BOOT or alter firmware boot order.

Published `boot-VERSION-native-installer.run` files are different: executing one is the **complete native installation operation**, not a build-only mode. It fetches a pinned repository commit, compiles using the native CPU profile, and performs installation. It can install prerequisite packages, elevate privileges, update the EFI System Partition (ESP), and modify UEFI NVRAM boot order. Its sole supported installation destination is:

```text
/EFI/InfiltratorOS/BOOT
```

The installed executable is `BOOTX64.EFI`. Reinstallation replaces the contents of BOOT's dedicated directory, including its previous configuration; it does not remove the separate Windows, GRUB or `EFI/BOOT` firmware paths.

The generic AMD64 `.deb` supplies the prebuilt EFI executable, six drivers and resources under `/usr/share/infiltrator-boot`. Its `postinst` installs them to the same fixed `/EFI/InfiltratorOS/BOOT` directory and registers BOOT first in UEFI BootOrder. It requires a mounted VFAT ESP and UEFI firmware access; it does not compile on the target host. A release publishes a `.run`, a `.deb` and `SHA256SUMS`; the release workflow publishes these as stable GitHub releases. This classification does not establish physical-hardware or production boot qualification.

## Verification and limits

`.github/workflows/boot.yml` compiles the EFI executable and drivers, verifies the release files, exercises installation and firmware-entry safeguards on disposable fixtures, and starts BOOT in QEMU/OVMF to confirm that the main menu can be reached. These checks do not prove compatibility with every real firmware, operating system, filesystem variant, Secure Boot configuration or direct-kernel boot path. The maintained loader-discovery and firmware interfaces descend from an established UEFI boot-manager implementation, but BOOT's current build, configuration, packaging and local modifications need their own qualification; inherited history is not evidence that this revision has been tested on an arbitrary number of computers.

A 22 November 2026 source-comment and documentation pass added technical contracts without changing executable C statements, firmware-variable behaviour, installer scripts or filesystem ABIs. See [Validation](docs/VALIDATION.md) for the exact comparison and automated-run evidence.

The definitive contracts, hardware coverage and precise test boundaries are maintained in [Architecture](docs/ARCHITECTURE.md), [Hardware](docs/HARDWARE.md) and [Validation](docs/VALIDATION.md). See the [documentation index](docs/README.md) and [Changelog](docs/CHANGELOG.md).


## Current documented risks

Linux native reinstallation intentionally replaces everything in BOOT's fixed directory; it does not preserve prior BOOT configuration or resources. The filesystem read and EFI allocation bugs identified in the source audit have received targeted corrections. EXT4 recovery-required volumes remain subject to the existing non-replaying read-only policy, and genuine hardware validation and targeted regression tests are still outstanding. See [Architecture](docs/ARCHITECTURE.md) and [Validation](docs/VALIDATION.md) for current behaviour and evidence boundaries.
