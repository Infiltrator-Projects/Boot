# Boot

Boot is the firmware-stage boot manager for InfiltratorOS. It is an Infiltrator project and is engineered under the canonical Infiltrator Engineering Philosophy.

The component name is **Boot**. The operating system is **InfiltratorOS**;
**Filesystems** provides filesystem capability information, and **Setup** is the
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
- `filesystems/` — firmware filesystem drivers and adapters, including the read-only InfiltratorFS integration;
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
- `docs/PROVENANCE.md` — historical source and licensing provenance;
- `BUILDING.md` — current build entry point;
- `CONTRIBUTING.md` — change discipline;
- `CHANGELOG.md` — Boot project history.

Historical imported documents such as `README.txt`, `BUILDING.txt` and `NEWS.txt` are retained for provenance and compatibility context. Where they conflict with maintained Infiltrator documentation, the maintained Infiltrator documentation governs product intent and engineering practice; licensing and attribution obligations remain authoritative regardless of document age.

## Licensing and attribution

The repository contains material under multiple licences inherited from its source history as well as Infiltrator-developed work. Attribution and licence obligations are preserved. See `docs/PROVENANCE.md`, `LICENSE.txt`, `COPYING.txt`, `CREDITS.txt`, and licence files in relevant subdirectories.

## Development preview and qualification

[VERSION](VERSION) identifies the Infiltrator development preview independently of the inherited engine version. The maintained [build entry point](BUILDING.md) and [validation contract](docs/VALIDATION.md) define its evidence. Main CI compiles the full GNU-EFI x86-64 program and read-only InfiltratorFS driver, reproduces pinned vendor source, and publishes the exact generated EFI binaries together with exact tracked source, source-commit identity and SHA-256 checksums.

The ready-to-copy Boot bundle is deliberately a Boot test surface rather than an assertion that every separately built firmware component is needed for ordinary OS handoff. The InfiltratorFS EFI driver remains available in the bundle under `EFI/BOOT/drivers-disabled/`; it is not placed in Boot's auto-loaded driver path unless a future qualified boot scenario actually requires it.

The published EFI files are development artifacts for controlled testing. They are not an installer and publishing them does not claim firmware-runtime, Secure Boot, persistent-installation or recovery qualification. Setup remains responsible for the eventual supported installation transaction.
