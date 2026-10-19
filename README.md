# Boot

Boot is the firmware-stage boot manager for InfiltratorOS. It is an Infiltrator project and is engineered under the canonical Infiltrator Engineering Philosophy.

The component name is **Boot**. The operating system is **InfiltratorOS**;
**Filesystems** owns filesystem implementations and drivers, and **Setup** is the
name reserved for the future installer. **InfiltratorFS** remains the name of the
filesystem format.

The repository was bootstrapped from a proven EFI codebase so that development could begin from working firmware machinery instead of recreating solved low-level mechanisms. That history is recorded in `docs/PROVENANCE.md`. It is provenance only: it does not define the product, its architecture, its user experience, its engineering standard, or its end state.

## Product contract

Boot exists to make the transition from firmware to the selected operating system safe, deterministic, understandable and effortless. It must discover valid boot paths, present them coherently, preserve platform trust boundaries, fail visibly rather than invent success, and avoid mutating firmware or persistent state without a justified and explicit contract.

InfiltratorOS is the product authority. External implementations, standards, firmware conventions and inherited mechanisms are evidence and mechanisms to study; none of them owns InfiltratorOS behaviour merely because it existed first.

## Engineering standard

This repository follows the canonical [Infiltrator Engineering Philosophy](https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/PHILOSOPHY.md): Mercedes-Benz engineering excellence as an execution benchmark — deep engineering hidden beneath a calm surface, one authority per concern, explicit ownership, proportional complexity, deterministic behaviour, designed failure and recovery, and technical documentation precise enough for another expert to maintain the system confidently.

The project-specific application of that standard is in `docs/PHILOSOPHY.md`.

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

The shared [Bootable Filesystem Integration Contract](https://github.com/Infiltrator-Projects/Filesystems/blob/main/docs/BOOTABLE_FILESYSTEM_CONTRACT.md) records how independent EFI filesystem readers, modular kernel-side filesystem modules and the future Setup installer cooperate. The first AmigaDOS milestone is to **load a Linux EFI-stub kernel stored on OFS**, with Linux's root remaining on a separately supported filesystem; that is not yet an OFS root installation.

## Maintained documentation

The maintained project documentation is intentionally separated by concern:

- `docs/PHILOSOPHY.md` — project-specific engineering rules;
- `docs/DESIGN.md` — product behaviour and interaction principles;
- `docs/ARCHITECTURE.md` — ownership, boundaries and current/target structure;
- `docs/PRODUCT-QUALITY.md` — acceptance standard and craftsmanship requirements;
- `docs/SAFETY.md` — firmware, persistence and trust-boundary safety rules;
- `docs/VALIDATION.md` — qualification and release evidence;
- [docs/LESSONS.md](docs/LESSONS.md) — lessons from component, packaging and icon-loading changes;
- `docs/PROVENANCE.md` — historical source and licensing provenance;
- `BUILDING.md` — current build entry point;
- `CONTRIBUTING.md` — change discipline;
- `CHANGELOG.md` — Boot project history.

Historical imported documents such as `README.txt`, `BUILDING.txt` and `NEWS.txt` are retained for provenance and compatibility context. Where they conflict with maintained Infiltrator documentation, the maintained Infiltrator documentation governs product intent and engineering practice; licensing and attribution obligations remain authoritative regardless of document age.

## Licensing and attribution

The repository contains material under multiple licences inherited from its source history as well as Infiltrator-developed work. Attribution and licence obligations are preserved. See `docs/PROVENANCE.md`, `LICENSE.txt`, `COPYING.txt`, `CREDITS.txt`, and licence files in relevant subdirectories.

## Development preview and qualification

[VERSION](VERSION) identifies the Infiltrator development preview independently of the inherited engine version. The maintained [build entry point](BUILDING.md) and [validation contract](docs/VALIDATION.md) define its evidence. Main CI builds Boot alone, verifies the runtime archive and DEB contain only its EFI application and presentation files, and publishes a DEB and checksum. Filesystem implementation source, build tooling and drivers are owned by Filesystems.

Boot bundles no filesystem drivers, including ext4. Install the required filesystem modules independently into the driver directory beside the selected Boot executable. Filesystems supplies these EFI modules; adding or updating one never requires rebuilding Boot.

The published EFI files are development artifacts for controlled testing. They are not an installer and publishing them does not claim firmware-runtime, Secure Boot, persistent-installation or recovery qualification. Setup remains responsible for the eventual supported installation transaction.

## Debian package and deliberate EFI deployment

Boot releases from v0.1.18 include `infiltrator-boot_VERSION_amd64.deb`,
built from the exact same verified UEFI ZIP bundle as the release executable.
The central Infiltrator APT repository can install and update this package.
The package owns its immutable EFI payload under
`/usr/lib/infiltrator-boot/EFI/BOOT/`, and the deploy helper at
`/usr/sbin/infiltrator-boot-deploy`.

Installing or upgrading the DEB **does not silently write to the EFI System
Partition**, install firmware variables, change BootOrder, or replace an
existing user `boot.conf` or independently installed filesystem drivers.
The existing-installation helper currently supports the Dell test layout:

```sh
sudo apt update
sudo apt install infiltrator-boot
sudo infiltrator-boot-deploy --update-existing --confirm
```

The deploy command requires a mounted FAT EFI System Partition at
`/boot/efi` and an existing regular
`/boot/efi/EFI/InfiltratorBoot/BOOTX64.EFI`. It refuses new destinations,
backs up the previous binary under a version-labelled name, installs a
checked replacement atomically in the same directory, and leaves
`boot.conf`, artwork and independently installed filesystem drivers unchanged.
It replaces only the EFI executable; it does not deploy the package's icons,
fonts or default configuration. A new packaged resource layout therefore does
not migrate an existing ESP installation.
This is not an automatic first-install firmware provisioning mechanism.
For other ESP layouts, preserve the package's staged payload and use a
separately verified and explicitly approved deployment plan.

## Public release asset policy

Starting with Boot v0.1.19, each GitHub release publishes exactly two
project-managed assets: `infiltrator-boot_VERSION_amd64.deb` and
`SHA256SUMS.txt`. GitHub also supplies its own automatic source archives.
The separately built EFI executable, theme graphics, source
snapshot and ready-to-copy UEFI ZIP remain verifiable **intermediate CI
outputs**, not public duplicates of the Debian package. The packaged
runtime files are staged beneath
`/usr/lib/infiltrator-boot/EFI/BOOT/`. The existing-installation helper updates
only the EFI executable; full resource deployment and migration belong to the
future Setup installation transaction. A release guard checks the exact two-asset
inventory before upload and validates the downloaded public bytes.
