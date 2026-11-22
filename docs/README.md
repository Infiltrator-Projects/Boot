# BOOT documentation

These documents follow the same division of responsibility as the System Monitor engineering documentation, but describe **BOOT's actual UEFI implementation**, not System Monitor's code.

**Design approach: ELEGANCE.** BOOT's artistic visual standard covers firmware presentation and the presentation of its own source and documentation. It is explicitly *not* minimalism or shorter commentary. The binding definition is in [Engineering Philosophy](PHILOSOPHY.md#elegance--the-design-approach).

## Maintained documents

- [Project README](../README.md): identity, capabilities, repository layout, build/install distinction and limitations.
- [Engineering Philosophy](PHILOSOPHY.md): quality, safety, fidelity and documentation standards.
- [Architecture](ARCHITECTURE.md): firmware runtime, scanning, EFI filesystem drivers, loader handoff, install ownership and release boundaries.
- [Hardware](HARDWARE.md): firmware, architecture, storage, filesystem and real-device compatibility constraints.
- [Validation](VALIDATION.md): executable tests, CI qualification, failure boundaries and hardware evidence.
- [Changelog](CHANGELOG.md): meaningful repository and behavioural changes.

## Ownership of technical truth

Source code and executable tests determine **implemented** behaviour. Architecture records current design and ownership; Hardware records device and filesystem capabilities and limits; Validation records what was actually exercised and which risks remain; the Changelog records dated changes. A desired Common V2 architecture must not be written as current BOOT functionality until code and integration tests establish it.

BOOT belongs to InfiltratorOS but must preserve independent operating systems and firmware boot choices. A machine with multiple supported installations must not have its discovery reduced to an assumed fixed menu. Local appearance and default-selection policies must not be confused with global bootability guarantees.

Documentation changes alone do not authorise runtime, installer, build-system or boot-order changes.

## Review status and scope

The source includes technical comments, but documentation completeness is evaluated by correctness and traceability rather than number of comment lines. The Architecture document records present-day call sequences and source-visible hazards; Validation distinguishes existing executable checks from missing regressions; Hardware separates emulation and observed hardware from unsupported claims. The documents are not a certification that every high-impact routine is fully documented or every known issue is fixed.

The 22 November 2026 source-comment pass covered 28 maintained C implementation files: BOOT lifecycle, configuration, discovery, menu, volume and legacy functions; EFI device paths and BDS; firmware graphics; FSW common/EFI bridges; EXT2, EXT4, NTFS, Btrfs, HFS, ISO 9660 and ReiserFS readers. It added 115 technical function contracts and moved ten comments from between declaration tokens to their proper positions. All changed source files were compared with revision `bf1888a`; with C comments and insignificant whitespace excluded, the remaining code matched. The generated resources, bundled third-party decoders and unrelated files were not rewritten for comment quantity.

The [Architecture](ARCHITECTURE.md) includes a call-boundary cross-reference; [Validation](VALIDATION.md) records the exact automated evidence and unresolved cases. Commentary is a statement of inspected behaviour, not a claim of a new runtime fix, universal compatibility or a completed Common V2 migration.
