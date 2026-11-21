# BOOT

BOOT is the UEFI boot manager supplied by **InfiltratorOS**. It is intended to discover and start every supported operating system installed on a computer, not to make InfiltratorOS exclusive and not to remove other operating systems or their firmware boot entries.

BOOT presents detected operating-system and loader choices from the firmware environment. The supported choices depend on the firmware, readable volumes, EFI loaders and kernel boot methods actually present. Discovery of many installations is a requirement; it must not be replaced by a hard-coded Windows/Linux/InfiltratorOS menu.

## Current implementation

The present source is a BOOT-branded, rEFInd-derived UEFI application built with GNU-EFI. The first-row operating-system menu, loader scanning, EFI launch path, Linux direct-kernel support, filesystem-driver loading and graphical presentation are implemented in `boot/` and associated libraries.

The standard AMD64 build produces `boot/boot_x64.efi` and six EFI filesystem drivers: EXT2, EXT4, ReiserFS, ISO 9660, HFS-family and Btrfs. Drivers are built from `filesystems/` and installed under BOOT's dedicated `drivers_x64/` directory. These are the **current UEFI drivers**; their existence must not be represented as proof that BOOT already loads Common V2 filesystem `.so` modules. The Common V2 transition is an architectural objective requiring separate implementation and validation.

The default configuration in `boot.conf-sample` hides GRUB's `grubx64.efi` and the standard EFI fallback loader from BOOT's graphical list, while leaving those files and their independent firmware boot paths in place. It prefers a directly discovered `vmlinuz` kernel when available. These display rules do not limit scanning to a particular number of installed operating systems.

BOOT has one intended official InfiltratorOS visual identity. The project is not being designed as a user-selectable theme collection. Existing graphics and configuration primitives remain in the code while the official appearance is developed.

## Repository layout

| Location | Responsibility |
| --- | --- |
| `boot/` | EFI entry point, discovery, configuration, menus, device/driver integration and EFI loader launch |
| `filesystems/` | Current EFI filesystem driver implementations and build profiles |
| `EfiLib/`, `libeg/`, `gzip/`, `mok/` | Firmware support, graphics, decompression and security-related integration |
| `include/`, `icons/`, `fonts/`, `banners/`, `keys/` | Shared headers and bundled resources |
| `boot-native-installer.run` | Sole native source-build-and-install program for the published AMD64 `.run` |
| `packaging/` | Generic staged package and source-pinned `.run` creation/verification |
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

The generic AMD64 `.deb` **stages** the executable, drivers and resources under `/usr/share/infiltrator-boot`. It does not install an EFI boot entry or modify the ESP. A release publishes a `.run`, a `.deb` and `SHA256SUMS`; the release workflow labels these builds as prereleases pending broader hardware qualification.

## Verification and limits

`.github/workflows/boot.yml` compiles the EFI executable and drivers, verifies the release files, exercises installation and firmware-entry safeguards on disposable fixtures, and starts BOOT in QEMU/OVMF to confirm that the main menu can be reached. These checks do not prove compatibility with every real firmware, operating system, filesystem variant, Secure Boot configuration or direct-kernel boot path.

The definitive contracts, hardware coverage and precise test boundaries are maintained in [Architecture](docs/ARCHITECTURE.md), [Hardware](docs/HARDWARE.md) and [Validation](docs/VALIDATION.md). See the [documentation index](docs/README.md) and [Changelog](docs/CHANGELOG.md).


## Current documented risks

The Linux native reinstall currently deletes BOOT's dedicated directory before the later SHA-256 ownership-marker guard can inspect the previous executable. The filesystem wrapper also has documented limits involving malformed EXT2 metadata, a failed mount allocation, 4 GiB stream-position narrowing and directory retry semantics. Comments identify these defects; they do not repair them. See [Architecture](docs/ARCHITECTURE.md) and [Validation](docs/VALIDATION.md) for source locations, current test coverage and missing regressions.
