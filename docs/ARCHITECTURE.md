# Architecture

## Purpose

This document is the architectural authority for Boot. It describes the ownership model the product is converging toward and maps the current source tree onto that model without treating historical directory names as design decisions.

The architecture is governed by `docs/PHILOSOPHY.md` and the canonical Infiltrator Engineering Philosophy.

## System responsibility

Boot owns the firmware-stage transition from platform firmware to a selected operating-system loader. Its responsibilities are deliberately narrow:

1. initialise the minimum firmware-facing mechanisms required for the product;
2. discover bootable candidates and supporting filesystems;
3. interpret product configuration deterministically;
4. present valid choices coherently;
5. transfer control to the selected loader under explicit trust and compatibility rules;
6. expose failures clearly enough to diagnose and recover; and
7. mutate persistent boot state only through separately owned, explicitly justified operations.

The Boot Manager is not a general-purpose operating system, package manager, filesystem repair utility or firmware configuration suite. Supporting mechanisms exist only to serve the boot contract.

## Authoritative concerns

### Boot-flow policy

Boot-flow policy owns startup sequencing, candidate ordering, selection, fallback and handoff. It must not be duplicated between UI code, scanner code, configuration parsing and platform-specific helpers.

### Candidate discovery

Discovery owns enumeration of devices, volumes, loaders and other boot candidates. Discovery reports evidence; it does not silently invent user policy. Ordering and default-selection policy belong to boot-flow policy.

### Configuration

Configuration owns parsing, validation and the representation of explicit user or system policy. Invalid and unsupported configuration must remain distinguishable from absent configuration. Consumers receive validated meaning rather than reparsing text independently.

### Presentation and input

Presentation owns visual hierarchy, input handling and selection interaction. It presents boot state; it does not become a second discovery or policy engine.

### Firmware boundary

Firmware-facing code owns UEFI protocols, handles, variables, memory and platform quirks. Product code should interact with firmware through narrow interfaces so firmware mechanics do not leak across unrelated concerns.

### Filesystem access

Filesystem drivers and adapters expose bounded read access required for discovery and loading. The default product contract is read-only. Filesystem policy belongs to the filesystem integration boundary, not to presentation or boot selection code.

The runtime filesystem extension contract is EFI Driver Binding + EFI Simple File System, **not** a named filesystem embedded in Boot's OS scanner. Boot loads all enabled qualified drivers in `drivers_x64` before scanning volumes, and asks firmware to bind them only to present BlockIo handles lacking SimpleFileSystem. This includes logical partitions **and native whole-disk layouts**; requiring `LogicalPartition` would rule out future Amiga RDB probing. Each driver must reject unsupported formats quickly through its EFI driver binding interface. Boot then rescans volumes and discovers boot files using the standard EFI file APIs. It does not parse ext, AmigaFS or another format in its UI or kernel handoff.

`filesystems/boot-driver-manifest.tsv` is the release packaging policy for each qualified driver: `enabled filename_x64.efi ReleaseArtifactPrefix` auto-loads, `disabled filename_x64.efi ReleaseArtifactPrefix` ships without executing. Release artifacts follow `ReleaseArtifactPrefix-VERSION-x86_64.efi`. An entry is valid only when the independently built, versioned driver exists and has passed qualification. New filesystem support must be introduced via an independently compiled EFI driver (including read-only semantics and format probing), an artifact-copy build step, and an explicit manifest entry. The manifest does not claim that an unimplemented AmigaFS driver exists. Disabled and enabled plugin inventories are verified exactly in the ZIP.

Driver startup must not reconnect every firmware device merely because a filesystem module was added. Broad controller binding is reserved for explicit custom non-filesystem driver paths or interactive rescans.

The cross-project [Bootable Filesystem Integration Contract](https://github.com/Infiltrator-Projects/Filesystems/blob/main/docs/BOOTABLE_FILESYSTEM_CONTRACT.md) is the agreed integration and phased-test reference. **Boot's readable kernel-source filesystem need not be the running OS's root filesystem.** Loading a Linux EFI-stub kernel and initramfs from Amiga OFS is an earlier, independent test from mounting an Amiga OFS Linux root. After handoff the runtime OS uses its own module ABI and filesystem drivers; Boot's EFI readers cannot replace Linux `.ko` modules. Future native InfiltratorOS may use a different kernel and payload protocol.

Filesystem capabilities and installation policies are supplied by Filesystems/Setup, not guessed by Boot's loader. Boot must not emulate missing filesystem semantics beyond the validated EFI read interface. Root suitability, POSIX capabilities and fail-closed installer adaptation are outside Boot's firmware stage.

### Trust and verification

Trust-sensitive mechanisms own signature, Secure Boot, MOK and related decisions where applicable. Trust state must be explicit and must not be reduced to a cosmetic warning after a decision has already been made.

### Persistent mutation

Operations that can alter NVRAM, boot order, ESP contents, partition metadata or other durable state are a separate authority from discovery. They require explicit preconditions, bounded effects, verification and recovery semantics.

### Build and qualification

Build orchestration owns how source becomes firmware artifacts. Qualification owns the evidence required to call an artifact acceptable. A build target existing in the repository does not by itself mean that target is release-qualified.

## Current source-tree mapping

The current implementation retains historical structure while ownership is progressively clarified:

- `refind/` currently contains the principal firmware application, including substantial boot-flow, configuration, scanning and presentation logic.
- `libeg/` provides graphics and related presentation mechanisms.
- `EfiLib/` contains EFI support code and firmware-facing mechanisms.
- `filesystems/` contains legacy firmware filesystem adapters. Boot builds only a transitional qualified ext4 reader. Filesystems owns builds, qualification and distribution of independent EFI drivers, including InfiltratorFS and OFS; Boot loads those drivers through the EFI protocol.
- `mok/` contains MOK/trust-related support used by the current implementation.
- `gzip/` contains compression support required by current firmware paths.
- `gptsync/` contains GPT/MBR synchronisation functionality. Because it can affect persistent disk state, it is conceptually separate from ordinary boot discovery even where historical build machinery groups it with the boot manager.
- `Makefile`, `Make.common`, per-directory makefiles and EDK2 metadata form the current build system.

This mapping describes current reality; it is not an endorsement of every existing boundary.

## Historical names

Identifiers such as `refind`, `RefindPkg`, `REFIND_SBAT_CSV` and similarly inherited names may remain while they participate in working build, firmware or compatibility contracts. They have no product-authority status.

Renaming them is justified when it accompanies a coherent boundary change, eliminates duplicated meaning, removes obsolete compatibility or establishes Infiltrator-owned semantics. Rename-only churn is rejected because it adds risk without engineering value.

## Dependency policy

A dependency is acceptable when its contract is stronger than a local reimplementation and its role is explicit. Product semantics remain product-owned.

Vendored dependencies must remain pinned and licensed. The legacy InfiltratorFS EFI adapter is retained as migration source but is no longer built or distributed by Boot.

Firmware code must not accidentally acquire operating-system runtime dependencies. EFI-specific CI therefore treats unresolved host-runtime symbols as a qualification failure.

## State and lifetime rules

Firmware execution has unusual lifetime and ownership constraints. New architecture must make the following explicit where relevant:

- who owns allocated firmware memory and when it is released;
- whether state survives only the current screen, current boot-manager invocation, or a firmware reboot;
- whether a value is discovered evidence, validated configuration, selected policy or persistent state;
- when UEFI handles/protocols are borrowed versus retained;
- which operations remain valid before and after `ExitBootServices` or equivalent transfer boundaries; and
- what happens when enumeration changes during the session.

Global mutable state should be reduced where it obscures those rules. Large files are not automatically defects; unclear ownership is.

## Failure semantics

Each authoritative concern must preserve useful failure distinctions. In particular:

- absent is not malformed;
- unsupported is not failed;
- inaccessible is not empty;
- untrusted is not missing;
- a candidate that cannot be verified is not equivalent to a verified candidate;
- a failed persistent mutation is not success with a warning.

Fallback may occur only where its ordering and meaning are documented and testable.

## Architectural migration rule

The project is not performing a ceremonial rewrite. Migration is incremental and evidence-driven.

A change that touches inherited code should leave ownership clearer than before. Prefer extracting an authority, removing a duplicate policy path, tightening an interface, making state explicit, or deleting obsolete machinery. Do not split cohesive code solely to make files smaller and do not introduce abstraction solely to hide historical names.

The end state is a boot manager whose architecture can be explained in product terms without reference to the implementation it originally started from.

## Build and source-preview ownership

The default GNU-EFI targets are explicit and independent of ambient EDK2/TianoCore installations. The product-owned build script locates GNU-EFI and compiles the Boot executable plus the transitional ext4 reader only. Make recursion preserves the chosen executable and flags. Generic cleanup owns repository outputs only.

Root VERSION owns source-preview identity; inherited engine versions remain separate. The single source workflow owns vendor verification, compilation and exact source artifacts. Publication promotes that sealed source as a prerelease. Binary release eligibility still belongs to firmware/runtime, trust and installation/recovery qualification.
