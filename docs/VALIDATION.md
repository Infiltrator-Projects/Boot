# Validation and Qualification

## Purpose

Validation establishes what the project actually knows about a change. It must not overstate support merely because source compiles or one automated job is green.

The validation standard is risk-proportional and follows the canonical Infiltrator Engineering Philosophy.

## Evidence classes

### Source and contract validation

This level checks structure, formatting where applicable, dependency identity, generated/vendor consistency, obvious ABI or symbol problems, and documentation consistency.

### Host build validation

This level proves that the selected firmware toolchain can produce the expected artifacts without unresolved host-runtime dependencies or build errors.

A successful host build proves compilation and linkage only. It does not prove firmware runtime behaviour.

### Firmware runtime validation

This level exercises the generated EFI application or driver in a representative firmware environment. Depending on the change, evidence may include virtual firmware, physical hardware, multiple firmware vendors, multiple architectures or Secure Boot configurations.

### Behaviour validation

This level verifies the product contract: discovery, ordering, configuration, input, presentation, loader handoff, fallback and useful failure states.

### Mutation and recovery validation

Any code capable of modifying NVRAM, boot order, ESP contents, partition metadata or other durable state requires dedicated tests for preconditions, interruption, verification and recovery. These paths must not be inferred safe from read-only discovery tests.

## Current automated qualification

The maintained development-preview workflow has the following automated coverage:

- building the full GNU-EFI x86-64 program through an explicit toolchain entry point;
- building the read-only InfiltratorFS EFI driver with GNU-EFI on x86-64;
- rejecting unresolved host operating-system/runtime dependencies in the resulting shared object;
- verifying that the committed vendored InfiltratorFS/Common/LZ4 tree matches the exact pinned source generator;
- verifying that the removable Boot bundle keeps the optional InfiltratorFS driver outside the auto-loaded driver path; and
- sealing the generated EFI binaries, tracked source archive and source-commit identity together with SHA-256 checksums before publication.

These checks establish source/build evidence and are deliberately not described as complete Boot runtime qualification. The presence of older GNU-EFI, TianoCore and EDK2 build paths in the repository does not make all of them release-qualified automatically.

## Current physical-firmware evidence

On 17 October 2026, Boot was exercised from a removable FAT32 EFI System Partition on a Dell Latitude 5550 using the firmware one-time boot menu. Secure Boot was disabled for this development test and Boot's configuration kept its own persistent state out of motherboard NVRAM.

That test established the following behaviour on real hardware:

- the Dell firmware successfully started Boot from `EFI/BOOT/BOOTX64.EFI` on removable media;
- Boot discovered EFI boot paths on the internal storage;
- selecting the Linux path successfully handed execution from Boot to the installed Linux system on the internal disk; and
- the removable-media Boot test did not require a kernel or operating-system payload on the removable card itself.

The first presentation test identified defects rather than boot-handoff defects: the inherited embedded banner remained visible, runtime icon lookups under the inherited `icons` subdirectory fell back to dummy graphics, and the 18px firmware font atlas was too small for comfortable use on the test display. Those observations drove the 0.1.8 presentation changes.

A second physical test with Boot 0.1.8 confirmed that the inherited banner was no longer visible, direct self-directory PNG lookup worked for recognised Windows/Ubuntu and utility artwork, the optional InfiltratorFS driver was absent from the active driver path, and the menu remained functional. It also showed that the presentation still did not meet the product-quality target: the inherited pale menu surface and grey selection treatment dominated the screen, volume badges and instructional hints added clutter, mixed inherited chrome remained visible, and the MB Corpo face appeared unnaturally widely tracked and low-contrast. Source review then identified two renderer-contract causes for the typography result: the proportional MB face was being forced into fixed-width cell advances, and the generated external atlas used a light master even though the renderer expects a dark master and performs its own inversion on dark backgrounds. Those observations are evidence for the 0.1.9 theme and proportional-font changes; they are not proof that the revised presentation is correct until 0.1.9 is retested on the same hardware.

InfiltratorFS was not required for the successful Linux handoff. The separately built InfiltratorFS EFI driver therefore remains outside the default auto-loaded driver path in the removable Boot bundle until a boot scenario that actually requires that driver is deliberately qualified.

## Required change evidence

### Documentation-only changes

Must remain internally consistent, preserve legal/provenance truth, and not claim validation that has not occurred.

### Build-system changes

Must prove that affected supported build paths still produce expected artifacts. Changes to dependency resolution or vendoring must prove exact identity and reproducibility.

### Discovery/configuration/UI changes

Must exercise both successful and malformed/unsupported input and confirm deterministic ordering or fallback where relevant.

### Filesystem changes

Must include malformed-volume behaviour, bounded parsing/allocation and read-only contract validation. A successful mount of a healthy sample is insufficient.

### Trust-sensitive changes

Must validate accept/reject behaviour across the affected signature or trust states and prove that failure cannot silently become execution.

### Persistent mutation changes

Must include target validation, partial-failure behaviour, post-verification and a recovery exercise. These changes require the highest scrutiny.

## Architecture matrix

The current source contains paths for x86-64, IA32 and AArch64. A release may claim an architecture only to the extent that the release candidate has current evidence for that architecture. Historical build support is not current evidence.

Where only one architecture is automated, documentation and release notes must say so plainly rather than implying universal qualification.

## Artifact identity

Where practical, the artifact released should be the exact artifact that passed qualification. Rebuilding after qualification creates a new artifact and therefore a new evidence problem unless reproducibility has been established strongly enough to prove identity.

Vendored dependency identity must be represented by exact immutable commits and corresponding licence material. Mutable tags or branch names are insufficient as the only dependency identity for release qualification.

## CI behaviour

CI is a verifier, not a hidden source-authoring process.

Automated jobs may generate temporary comparison state in isolated directories, but they must not silently rewrite product source, create commits or push generated state to `main` merely because a workflow ran. Source changes should remain explicit repository changes subject to ordinary review and history.

## Release gate

Before release, answer with evidence:

- What product behaviour changed?
- Which architecture/toolchain combinations were built?
- Which firmware/runtime environments were exercised?
- Which failure states were tested?
- Did any trust or persistent-state authority change?
- Are vendored dependencies exact and reproducible?
- Are documentation and release claims limited to what the evidence proves?

A release is not qualified if those questions cannot be answered confidently for the risk introduced by the change.

## Development-preview publication

Root VERSION identifies the development preview; inherited engine/profile versions retain their separate meaning. The same successful workflow seals the generated GNU-EFI x86-64 Boot application, read-only InfiltratorFS EFI driver, tracked Git source and source-commit identity with SHA-256. Its publisher downloads that artifact, verifies the checksum manifest, compares complete remote bytes before and after publication, and publishes the verified release bytes.

The generated EFI files are published for controlled testing because they are the exact artifacts that passed the current host-build checks. Their publication does not elevate host-build evidence into universal firmware-runtime, Secure Boot, installation or recovery qualification. No automatic installation is included, and the workflow must not modify an EFI System Partition or NVRAM.

## Physical Linux EFI-stub handoff (Boot 0.1.10)

On the Dell Latitude 5550, Boot from the removable SD using its active read-only ext4 driver displayed the installed 800 GiB Linux Mint root and successfully launched `vmlinuz-7.0.0-38-generic` with matching initrd. The running kernel reported `ro root=UUID=7d52ddd4-d8e5-40b1-910d-b81d5ee3176e initrd=boot\\initrd.img-7.0.0-38-generic`, confirming a minimal direct handoff without the former GRUB command line. The installed InfiltratorOS Plymouth boot theme did not appear because automatically inferred options omitted `quiet splash` and the default graphical-mode policy switched UEFI to text before launching Linux.

Boot 0.1.11 source restores graphics-mode Linux handoff and conditionally requests the installed Plymouth theme for auto-inferred normal entries while leaving recovery choices verbose. Physical splash appearance remains **unverified** until retested; build verification alone does not qualify it. The separate graphics/font variation reported on identical Boot builds across reboots is **not** resolved by this handoff fix and must be diagnosed independently with consistent GOP mode and asset-load evidence.

## Cold-start icon read qualification (Boot 0.1.12)

Observed on a Dell Latitude 5550 with Boot 0.1.11 installed on SD: a cold boot rendered yellow-striped placeholder icons; restarting Linux and warm-booting exactly the same SD build restored the expected OS and utility icons. The product banner, font and direct ext4 kernel discovery were present on both. Separate Linux evidence of SD controller resume timeouts, removal/re-enumeration and corrupted FAT32 metadata raises a hardware/firmware concern, but is not proof of the icon failure's cause.

0.1.12 converts icon path `.` into a direct root-relative filename, retries only actual EFI transient read errors (up to twice), checks for short EFI_SUCCESS reads and null decodes, and shows volatile fallback/recovery/error counters and status on-screen. Ordinary missing optional icons are not retried and their EFI_NOT_FOUND reports are not classified as hardware errors. Boot-specific state reads/writes with `use_nvram false` cannot create an SD or internal-ESP `vars` directory. This does not suppress intentional firmware-management operations.

Cold and warm photographs, boot selection and subsequent `cat /proc/cmdline` are required for runtime qualification. Successful CI cannot establish reliable SD hardware or firmware behavior.

## Boot 0.1.13 RAM-only pre-menu performance investigation

The Dell Latitude 5550, launched using the internal ESP `EFI/InfiltratorBoot/BOOTX64.EFI` and retaining GRUB as the default, showed approximately 17.5–17.8s in systemd-analyze's `loader` column across two tests. Its operator observed the apparent delay before Boot's graphical screen. A change from menu `timeout 0` to `timeout 3` did not materially change that column. Those observations warrant measurement but do not definitively assign every measured loader second to Boot's own startup.

0.1.13 reports two volatile lines in the graphical Boot menu with timing categories (disks, config, drivers, re-scan, graphics, OS discovery, total), ending before the menu wait. The implementation uses the UEFI runtime clock and may show only approximately second-resolution data on firmware with coarse RTC GetTime; do not interpret sub-second values as precise in that case. No disk logs or EFI variables are written by the measurement.

For a useful test, photograph the on-screen timing lines from a cold internal-SSD Boot launch. Compare the reported total and largest stage against the Linux `systemd-analyze time` loader phase after entering Linux. If the on-screen measurement is unavailable or clearly inconsistent, instrument with an EFI high-resolution timing source separately rather than optimizing guessed hotspots. Do not remove ext4 drivers or broad OS discovery until the bottleneck is identified. GRUB remains first in permanent BootOrder.
