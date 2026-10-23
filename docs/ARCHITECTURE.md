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

The runtime filesystem extension contract is EFI Driver Binding + EFI Simple File System, **not** a named filesystem embedded in Boot's OS scanner. Boot loads independently installed drivers in `drivers_x64` before scanning volumes, and asks firmware to bind them only to present BlockIo handles lacking SimpleFileSystem. This includes logical partitions **and native whole-disk layouts**; requiring `LogicalPartition` would rule out future Amiga RDB probing. Each driver must reject unsupported formats quickly through its EFI driver binding interface. Boot then rescans volumes and discovers boot files using the standard EFI file APIs. It does not parse ext, AmigaFS or another format in its UI or kernel handoff.


A Linux EFI-stub image normally uses UEFI `LoadImage` with a device path.
When firmware cannot resolve a kernel path exposed by an external EFI filesystem
driver but Boot's EFI file protocol can still read it, Boot makes a bounded
Linux-only memory-backed retry after `EFI_NOT_FOUND` or `EFI_DEVICE_ERROR`,
and only after the complete file is read with the expected size through the
existing EFI file protocol. Security, invalid-image and other firmware errors
are not retried. Boot passes the original
device path alongside the memory bytes: the path remains authoritative for
kernel/initramfs filesystem context, and the firmware remains responsible for
PE/COFF validation, policy enforcement and image execution. No OFS-specific
path rules or filesystem parsers are added to Boot.

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
- Filesystem implementation source and driver builds reside in Filesystems, including the migrated firmware adapters under `firmware/uefi/`. Boot has no filesystem implementation directory, bundled driver or filesystem build target. Its generic loader consumes independently installed modules through UEFI interfaces.
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

Vendored dependencies must remain pinned and licensed. All firmware filesystem adapters and their pinned portable source are maintained in Filesystems; Boot has no vendored filesystem core.

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

The default GNU-EFI targets are explicit and independent of ambient EDK2/TianoCore installations. The product-owned build script locates GNU-EFI and compiles the Boot executable without compiling or fetching any filesystem driver. Make recursion preserves the chosen executable and flags. Generic cleanup owns repository outputs only.

Root VERSION owns source-preview identity; inherited engine versions remain separate. The single source workflow owns vendor verification, compilation and exact source artifacts. Publication promotes the exact verified Debian package and checksum under a new immutable version. Binary release eligibility still belongs to firmware/runtime, trust and installation/recovery qualification.


### Opt-in manual memory-first Linux EFI-stub loading

Only a manual `menuentry` can request `linux_load memory`; otherwise the
Linux kernel path remains the existing device-path-first firmware load
(`linux_load path`). Memory-first reads bounded, verified 64 KiB chunks
through the EFI Simple File System protocol of the explicitly selected volume,
shows confirmed progress, and calls firmware LoadImage once with both the
complete memory buffer and the **original device path**. This retains source
filesystem context for `initrd=` and firmware image security validation.
Unknown modes disable the manual entry; the memory option on a non-Linux
target also disables it. Neither the independent filesystem drivers nor other
loaders change. A blocked synchronous `EFI_FILE_PROTOCOL.Read` cannot
provide heartbeat callbacks or be interrupted by the Boot UI.

## Portable storage-controller driver stage — 0.1.34

Boot loads optional standalone UEFI storage transport drivers in
`storage_x64/` (or legacy `storage/`) **before** loading filesystem drivers
from `drivers_x64/`. All paths are relative to the currently running Boot
executable. Missing storage directories are a no-op: Boot retains the
firmware-provided EFI BlockIo/DiskIo stack on ordinary computers.

A native storage transport driver must opt into the system using standard
UEFI Driver Binding and publish standard block-device handles so the
existing filesystem parsers can consume them unchanged. Because Boot's
general filesystem-connect pass intentionally does not enumerate and
reconnect unrelated PCI controllers, a standalone native provider is
responsible for binding its **own matching controller** before returning
success from its EFI entry point. Boot does not embed Realtek/Intel
device IDs, perform PCI configuration writes, or silently replace firmware
storage drivers. Every hardware-specific transport driver requires
independent physical and regression qualification before automatic
deployment. A firmware BlockIo implementation which pauses for 30
seconds cannot be interrupted by a filesystem-level timeout.

This is an architectural separation and loading-order correction, **not**
a claim that a native Dell RTS525A transport exists or that the physical
OFS SD timeout has been resolved.
