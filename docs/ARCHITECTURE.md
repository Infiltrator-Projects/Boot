# Architecture

## Purpose and execution boundary

BOOT is the UEFI boot manager owned by InfiltratorOS and intended to launch **other supported operating systems as well as InfiltratorOS**. It is a firmware-stage application, not an installed operating-system kernel, Linux VFS implementation or replacement for every other firmware boot entry. It must not assume a fixed list of installed systems.

The present executable is `boot/boot_x64.efi`, packaged and installed as `/EFI/InfiltratorOS/BOOT/BOOTX64.EFI`. The current main AMD64 build uses GNU-EFI. Some source files still contain conditional code and alternate firmware build infrastructure; their presence does not establish independently qualified releases for those architectures.

## From firmware entry to a selected loader

| Stage | State produced | Why the order matters |
| --- | --- | --- |
| `efi_main` calls `ScanVolumes`, then `ReadConfig` | A volume inventory and the configured scan order | Configuration can refer to a volume by name. Driver loading follows, and a positive `LoadDrivers` result triggers a fresh volume scan. |
| `ScanForBootloaders` visits the configured sources | Menu entries carry either a volume and file path or a firmware option number | Directory candidates are sorted before kernel folding. A folded sibling keeps its own path and options in the submenu. |
| `RunMainMenu` returns a selection and an exit reason | A pointer into the current menu inventory | Escape can rebuild that inventory. The old pointer belongs to the menu that supplied it. |
| `efi_main` dispatches on the entry tag | A child EFI image or a request for a firmware reset | A direct loader and a firmware boot option can have similar titles and still require different firmware calls. |

The branch point is visible in the two cases below. [Entry point](../boot/main.c) · [Menu records](../boot/global.h) · [Loader discovery](../boot/scan.c)

**Selecting a Linux EFI-stub file.** `SetLoaderDefaults` asks `GetMainLinuxOptions` for the primary command line. The first options-file entry supplies the initial arguments. If the kernel directory has no readable options file, the parser tries the volume's fstab and then the discovered root partition. `FindInitrd` searches beside the selected kernel and matches its extracted version. `StartLoader` eventually passes the path and resulting options to `StartEFIImage`. Firmware's loaded-image protocol receives the options length in bytes, including the final CHAR16 terminator. [Options parser](../boot/config.c) · [Kernel and initrd selection](../boot/linux.c) · [EFI handoff](../boot/launch_efi.c)

**Selecting a firmware entry.** Suppose the menu entry represents `Boot0007`. The entry contains numeric value 7. `RebootIntoLoader` writes that two-byte value to `BootNext` under the EFI global-variable GUID and requests a cold reset only after the write succeeds. The next firmware boot interprets `Boot0007`. Changing the persistent order uses a separate function, `SetBootDefault`. For example, promoting 7 in `[2, 7, 4, 7]` produces `[7, 2, 4]`. [Firmware option records](../boot/install.c) · [One-time selection](../boot/launch_efi.c)

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

The destination path is strictly `/EFI/InfiltratorOS/BOOT`, and the native executable's filename is `BOOTX64.EFI`. The installer contains a fixed-path and symbolic-link guard and a SHA-256 ownership-marker check (`.infiltrator-boot-loader.sha256`). These checks are effective only when run before the original executable is removed; see the Linux ordering defect below. On a Linux-host installation, `InstallOnLinux` currently removes and recreates BOOT's dedicated directory, so **existing BOOT configuration and assets in that directory are not preserved**. Critically, the Linux code calls `rm -rf` **before** `CopyBootFiles` invokes `GuardDefaultBootLocation`: the guard therefore cannot verify the previous loader's ownership marker before that deletion. This is a known ordering defect, not an established pre-deletion protection. The Windows, GRUB and standard `EFI/BOOT` paths remain outside this deletion scope.

The installer calls its firmware-entry management routines to establish BOOT as the preferred firmware choice. The tests exercise detection of duplicate/stale entries, preservation of non-BOOT entries and repeated installation ordering; those are distinct from the BOOT runtime menu's choices. Failure to identify ownership or firmware state correctly must not be papered over with a second BOOT entry.

The generic AMD64 `.deb` does not execute these installation operations. Its responsibility is staging the firmware image, drivers, configuration sample and other resources under `/usr/share/infiltrator-boot`. Packaging it is not equivalent to installing a UEFI boot manager.

## Build and release ownership

`Makefile` calls the GNU-EFI build for `libeg/`, `mok/`, `gzip/`, `EfiLib/`, `boot/` and `filesystems/`. `packaging/build-release.sh` pins the exact source commit in the downloadable `.run` and creates a staged `.deb` plus checksums. `packaging/test-release.sh` verifies artifact metadata, expected driver contents and native-install structure.

`.github/workflows/boot.yml` owns current AMD64 verification and automated prerelease publication. The source tree also includes older or alternative make targets; only the explicit native release path is authoritative for the shipped `.run`.

## Graphical presentation and product identity

The menu code can load supplied icons, banners, fonts, selection images and other resources. Those are lower-level capabilities of the present firmware renderer, not approval for a user-installable theme ecosystem. BOOT is intended to carry one official InfiltratorOS visual design. The optional Snowy assets have been removed from the repository; the basic icon/graphics infrastructure remains.

Appearance changes must not alter scan ordering, delete other loaders, disable filesystem drivers or change the native installer. A missing image cannot be allowed to invalidate an otherwise sound boot path.

## Integrity and trust boundaries

There are distinct integrity boundaries: the downloaded source is pinned to the release commit; the installer contains an existing-binary ownership-marker check, although its Linux reinstall ordering currently bypasses that check for the deleted prior directory; EFI images and drivers are handled through firmware execution and security mechanisms; and Secure Boot acceptance depends on the firmware's active trust configuration. A SHA-256 ownership marker is **not** equivalent to Secure Boot authentication.

Input surfaces include ESP directory entries, filesystem metadata, configuration text, EFI load options, firmware NVRAM state and optional hardware-dependent paths. Malformed or missing data must not be represented as successful OS discovery, authenticated execution or a verified firmware update.

## Unverified architectural objectives

The desired Common V2-only filesystem module design, universal operating-system compatibility, production-grade security qualification and complete custom InfiltratorOS appearance require explicit future evidence. This document records the implemented BOOT revision and does not silently declare those objectives done. See [Hardware](HARDWARE.md) and [Validation](VALIDATION.md) for observed limits.


## State, resource ownership and failure contracts

The following table records code-level relationships rather than a promise of complete correctness. A source file may implement additional paths not represented here.

| Transition | Inputs and mutable state | Ownership / failure consequence | Primary implementation |
| --- | --- | --- | --- |
| Firmware entry → volume scan | EFI image handle, system table, controller protocol database | Volume records depend on firmware handles and successful protocol acquisition; an inaccessible filesystem is not an empty one | `boot/main.c`, `boot/lib.c` |
| Configuration → menu inventory | Config bytes, volume identifiers, scan/exclusion flags | Parser state determines visibility. Hiding a candidate does not delete an EFI file or NVRAM record | `boot/config.c`, `boot/scan.c` |
| Menu selection → action | Selected menu record, entry tag, current inventory | Menu entries and their submenus belong to the inventory that produced them; a rescan invalidates previous selection pointers | `boot/menu.c`, `boot/main.c`, `boot/global.h` |
| EFI launch | Candidate path, volume handle, optional UTF-16 load options | EFI image classification precedes authentication/firmware launch. Option buffers must survive the firmware call and statuses must propagate | `boot/launch_efi.c` |
| Firmware Boot#### selection | Binary NVRAM option and device-path metadata | `BootNext` is a one-shot firmware selection; persistent `BootOrder` management is distinct and has its own failure modes | `boot/install.c`, `boot/launch_efi.c` |
| Filesystem mount/read | Block and Disk I/O protocols, on-disk metadata, FSW dnodes/streams | Per-volume caches and open streams have separate lifetimes; a recognised magic number is not proof of a valid directory or file | `filesystems/fsw_efi.c`, `filesystems/fsw_core.c` |
| Native reinstall | ESP path, old BOOT directory, ownership marker, boot entry records | The Linux path currently clears the old BOOT directory *before* its later marker guard. This is a known destructive-ordering exposure | `boot-native-installer.run` |

### Filesystem read and error boundaries

The FSW core expresses a file as a stream over logical extents. A physical extent translates through block caching, a buffered extent provides decoder- or backend-owned bytes, and a sparse extent supplies zero bytes. The backend's extent units and the host's physical block units must remain distinct. Open streams retain independent positions even where they share a volume cache. See `filesystems/design.dox` for a worked block-boundary example.

This is a reader design, **not Common V2 conformance**. The shipped driver-binding layer publishes UEFI Simple File System protocols. It does not authenticate, dynamically link or execute Common V2 `.so` modules.

### Known correctness limits visible in source

These are findings from static inspection; no new test result is implied.

1. **Reinstall-before-ownership-check:** `InstallOnLinux` deletes BOOT's directory before the ownership guard inside `CopyBootFiles` runs. Mocking the guard separately does not establish pre-deletion safety.
2. **EXT2 untrusted divisor:** `fsw_ext2_volume_mount` uses `s_inodes_per_group` in a division without an immediately preceding nonzero check. A malformed superblock can trigger invalid arithmetic.
3. **FSW mount allocation:** `fsw_efi_DriverBinding_Start` dereferences `Volume` immediately after `AllocateZeroPool` without a null check in that path.
4. **Large-file offset narrowing:** `fsw_shandle_read` casts a 64-bit stream position to `fsw_u32` before its read loop. At or above 4 GiB this can address the wrong data.
5. **Directory retry semantics:** `fsw_efi_dir_read` can advance the directory cursor before discovering that `EFI_FILE_INFO` will not fit in the supplied buffer. A size-probe/retry can skip an entry.
6. **EXT4 journal state:** `fsw_ext4_volume_mount` admits the RECOVER incompatibility flag but does not replay a journal; this may expose inconsistent on-disk state.
7. **Controller classification index:** the GNU-EFI `ConnectAllDriversToAllControllers` path indexes the per-query `HandleType` array using an index from the outer handle enumeration. The ranges need not match and the index correspondence is not established in that function.
8. **Legacy targets:** `filesystems/Makefile` retains an independent `install` recipe targeting `EFI/refind/drivers`; the POSIX filesystem test makefile contains XFS object references not satisfied by the current source tree.

These findings should not be disguised as resolved by explanatory comments. The exact limitations and evidence requirements are carried into [Validation](VALIDATION.md).
