# Architecture

## Purpose

This document is the architectural authority for InfiltratorOS Boot Manager. It describes the ownership model the product is converging toward and maps the current source tree onto that model without treating historical directory names as design decisions.

The architecture is governed by `PHILOSOPHY.md` and the canonical Infiltrator Engineering Philosophy.

## System responsibility

InfiltratorOS Boot Manager owns the firmware-stage transition from platform firmware to a selected operating-system loader. Its responsibilities are deliberately narrow:

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
- `filesystems/` contains firmware filesystem drivers and adapters. The InfiltratorFS path vendors an exact portable core and exposes it through a read-only EFI integration.
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

Vendored dependencies must be pinned to exact source identities, carry applicable licensing, and have a reproducible generation/verification path. The InfiltratorFS UEFI integration follows this rule through `scripts/vendor-infiltratorfs.sh` and the corresponding CI verification workflow.

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
