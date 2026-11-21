# BOOT documentation

These documents follow the same division of responsibility as the System Monitor engineering documentation, but describe **BOOT's actual UEFI implementation**, not System Monitor's code.

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
