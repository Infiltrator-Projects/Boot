# Boot

BOOT extends firmware filesystem access with [Common V2 shared objects](docs/FILESYSTEM_SO_LOADER_CONTRACT.md). Each format has one independently owned engine. Application and firmware support depend on the capabilities that engine implements.

Boot is the firmware-stage boot manager for InfiltratorOS. It is an Infiltrator project and is engineered under the canonical Infiltrator Engineering Philosophy.

The component name is **Boot**. The operating system is **InfiltratorOS**;
The independent filesystem repositories own their Common V2 engines. **InfiltratorFS** owns its native format and Linux driver. **Setup** is the
name reserved for the future installer. **InfiltratorFS** remains the name of the
filesystem format.

The repository was bootstrapped from a proven EFI codebase so that development could begin from working firmware machinery instead of recreating solved low-level mechanisms. That history is recorded in `docs/PROVENANCE.md`. It is provenance only: it does not define the product, its architecture, its user experience, its engineering standard, or its end state.

## Product contract

Boot exists to make the transition from firmware to the selected operating system safe, deterministic, understandable and effortless. It must discover valid boot paths, present them coherently, preserve platform trust boundaries, fail visibly rather than invent success, and avoid mutating firmware or persistent state without a justified and explicit contract.

InfiltratorOS is the product authority. External implementations, standards, firmware conventions and inherited mechanisms are evidence and mechanisms to study; none of them owns InfiltratorOS behaviour merely because it existed first.

## Engineering standard

This repository follows the canonical [Infiltrator Engineering Philosophy](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/PHILOSOPHY.md): Mercedes-Benz engineering excellence as an execution benchmark — deep engineering hidden beneath a calm surface, one authority per concern, explicit ownership, proportional complexity, deterministic behaviour, designed failure and recovery, and technical documentation precise enough for another expert to maintain the system confidently.

The project-specific application of that standard is in `docs/PHILOSOPHY.md`.

## Verified InfiltratorOS boot-chain milestone

On 8 November 2026, the [real three-repository QEMU/OVMF integration run](https://github.com/Infiltrator-Projects/Kernel/actions/runs/42431603931) passed for **Boot 0.1.39**, InfiltratorFS Format 0.19 source `c8f6431f2de34806d8614a241cab8db5a0daad48`, and the real InfiltratorOS Kernel `0.1.0-infiltrator`. The FAT32 ESP contained only Boot, its configuration and the InfiltratorFS EFI filesystem reader. Boot loaded both kernel and initramfs through the native InfiltratorFS UEFI filesystem driver. The kernel started and loaded the ABI-matching native filesystem module, mounted and read/wrote InfiltratorFS. Evidence includes QEMU serial output.

This historical VM result applies to Boot 0.1.39 and its then-supported EFI reader. Direct InfiltratorFS boot on current BOOT requires a firmware-compatible Common V2 module. The pinned historical qualification lives in the Kernel repository.

## Current implementation

The current tree deliberately retains some historical directory, file and symbol names while the implementation is progressively reorganised. Names such as `refind`, `RefindPkg` and related build identifiers are therefore implementation history, not product identity or architectural authority. They should be replaced only when doing so improves a real boundary or removes real debt; rename-only churn is not a quality improvement.

The current major implementation areas are:

- `refind/` — the present firmware application and boot-flow implementation;
- `libeg/` — graphics and presentation support;
- `EfiLib/` — EFI support mechanisms;
- `mok/` — trust and MOK-related support inherited by the current implementation;
- `gzip/` — compressed-data support used by the firmware application;
- `gptsync/` — GPT/MBR synchronisation utility code retained by the current tree;
- `Makefile` and `Make.common` — current build orchestration, including GNU-EFI and inherited TianoCore/EDK2 paths.

The authoritative architectural interpretation of those areas is `docs/ARCHITECTURE.md`.

BOOT 0.1.46 reached Linux in OVMF using the released `OFS.so` to read the kernel and initramfs from an independently generated Amiga OFS image. Linux started from the initramfs. Mounting OFS as a Linux root through a generic Common V2 bridge remains separate work. See the [Bootable Filesystem Integration Contract](https://github.com/Infiltrator-Projects/Filesystems/blob/main/docs/BOOTABLE_FILESYSTEM_CONTRACT.md).

## Maintained documentation

The maintained project documentation is intentionally separated by concern:

- `docs/PHILOSOPHY.md` — project-specific engineering rules;
- `docs/DESIGN.md` — product behaviour and interaction principles;
- `docs/ARCHITECTURE.md` — ownership, boundaries and current/target structure;
- `docs/PRODUCT-QUALITY.md` — acceptance standard and craftsmanship requirements;
- `docs/SAFETY.md` — firmware, persistence and trust-boundary safety rules;
- `docs/VALIDATION.md` — qualification and release evidence;
- `docs/DRIVER-DIAGNOSTICS.md` — opt-in physical firmware driver tracing and recovery;
- [docs/LESSONS.md](docs/LESSONS.md) — lessons from component, packaging and icon-loading changes;
- `docs/PROVENANCE.md` — historical source and licensing provenance;
- `BUILDING.md` — current build entry point;
- `CONTRIBUTING.md` — change discipline;
- `CHANGELOG.md` — Boot project history.

Historical imported documents such as `README.txt`, `BUILDING.txt` and `NEWS.txt` are retained for provenance and compatibility context. Where they conflict with maintained Infiltrator documentation, the maintained Infiltrator documentation governs product intent and engineering practice; licensing and attribution obligations remain authoritative regardless of document age.

## Licensing and attribution

The repository contains material under multiple licences inherited from its source history as well as Infiltrator-developed work. Attribution and licence obligations are preserved. See `docs/PROVENANCE.md`, `LICENSE.txt`, `COPYING.txt`, `CREDITS.txt`, and licence files in relevant subdirectories.

## Development preview and qualification

[VERSION](VERSION) identifies the Infiltrator development preview independently of the inherited engine version. The maintained [build entry point](BUILDING.md) and [validation contract](docs/VALIDATION.md) define its evidence. Main CI verifies the runtime payload and publishes a portable DEB, a source-bearing native installer and checksums. Filesystem implementation source, build tooling and drivers are owned by Filesystems.

Boot's x86-64 loader consumes filesystem `.so` modules through Common V2 and a read-only UEFI bridge. Stage the same module bytes used by applications in `filesystems_x64` beside Boot's EFI executable. A volume requires either a firmware-provided reader or a compatible Common V2 module. See the [loader contract](docs/FILESYSTEM_SO_LOADER_CONTRACT.md).

The published EFI files are development artifacts for controlled testing. They are not an installer and publishing them does not claim firmware-runtime, Secure Boot, persistent-installation or recovery qualification. Setup remains responsible for the eventual supported installation transaction.

## Debian package and deliberate EFI deployment

Boot releases contain one independently installable
`infiltrator-boot_VERSION_amd64.deb` and `SHA256SUMS.txt`. The Debian
package owns the complete EFI payload beneath
`/usr/lib/infiltrator-boot/EFI/BOOT/`, the privileged command
`/usr/sbin/infiltrator-boot-deploy` and its internal deployment engine.
**Installing or upgrading the DEB never writes to an EFI System Partition
or changes firmware variables or BootOrder.**

On a machine with a verified vfat ESP mounted at `/boot/efi`, the operator
can explicitly choose one operation:

```sh
sudo apt install infiltrator-boot
sudo infiltrator-boot-deploy --install-new --confirm
# For an existing /EFI/InfiltratorBoot installation instead:
sudo infiltrator-boot-deploy --update-existing --confirm
# Revert a completed or interrupted update using the previous full copy:
sudo infiltrator-boot-deploy --recover --confirm
```

A **new** installation creates `/boot/efi/EFI/InfiltratorBoot/` with
`BOOTX64.EFI`, the default `boot.conf`, fonts, graphical resources,
`icons/`, and a `filesystems_x64/` directory for independently installed modules.
It never overwrites an existing installation or any other EFI loader.
A firmware boot entry is **not** created. Use the firmware's Boot from File
feature or a separately approved and verified firmware-entry step to select
the installed executable. Copying files is not proof of a successful UEFI boot.

An **update** builds and byte-verifies a complete candidate under a sibling
directory on the same ESP. It replaces the BOOT executable and package-owned
graphics together while preserving an existing user `boot.conf`, custom
artwork, separately installed resources and filesystem SOs. The former EXT4
EFI driver is retired when the independently installed Common V2 `EXT4.so`
passes validation and the host EXT4 boot layout is compatible. Unknown EFI
readers or custom external reader settings keep the current installation in
place. The previous complete installation remains under
`EFI/.InfiltratorBoot.rollback` and can be restored with `--recover --confirm`.

On a GPT EXT4 volume opened by the V2 module, BOOT automatically discovers
the newest versioned InfiltratorOS image in `/System/Kernel/Images/` and uses a matching
native initramfs when present. The regular EFI and Linux scans continue to
populate the same menu.

A directory swap on a FAT ESP requires two renames and **cannot be perfectly
atomic across sudden power loss**. If interrupted between those renames, the
previous directory remains at `EFI/.InfiltratorBoot.rollback`; the recovery
command restores the normal EFI path. The deployment engine attempts automatic
rollback on ordinary script failures. Keep the separately verified original OS
boot path intact during qualification. The public wrapper requires root, a
mounted vfat ESP at exactly `/boot/efi`, and explicit confirmation. It does
not alter NVRAM or unrelated EFI directories.

`bash tests/check-boot-deploy.sh` runs host-level tests using disposable
directory trees. Those tests cover first installation, upgrades, preservation
of independent resources, bad input, simulated promotion failure and
interrupted-swap recovery. They do not replace physical firmware, ESP
power-loss or Secure Boot qualification. Setup remains the owner of the future
system-wide installation and firmware-entry workflow.

## Public release asset policy

Starting with Boot v0.1.43, each GitHub release publishes
`infiltrator-boot_VERSION_amd64.deb`, `boot-VERSION-native-installer.run` and
`SHA256SUMS.txt`. GitHub also supplies its own automatic source archives.
The separately built EFI executable, theme graphics, source
snapshot and ready-to-copy UEFI ZIP remain verifiable **intermediate CI
outputs**, not public duplicates of the Debian package. The packaged
runtime files are staged beneath
`/usr/lib/infiltrator-boot/EFI/BOOT/`. The opt-in deployment command installs or updates full managed BOOT resources;
future Setup owns firmware registration and whole-system installation. A release guard checks the exact three-asset
inventory before upload and validates the downloaded public bytes.
