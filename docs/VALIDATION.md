# Validation and Qualification

## Purpose

Validation establishes what the project actually knows about a change. It must not overstate support merely because source compiles or one automated job is green.

The validation standard is risk-proportional and follows the canonical Infiltrator Engineering Philosophy.

## Evidence classes

### Source and contract validation

This level checks structure, formatting where applicable, dependency identity, generated/vendor consistency, obvious ABI or symbol problems, and documentation consistency.

### Host build validation

This level proves that the selected firmware toolchain can produce the Boot application without unresolved host-runtime dependencies or build errors.

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
- verifying that Boot contains no filesystem implementation directory and compiles no EFI filesystem driver;
- rejecting unresolved host operating-system/runtime dependencies in the resulting shared object;
- exercising the production icon reader and image decoders against mocked EFI protocols, including directory ownership, retries and short reads;
- verifying the runtime ZIP and DEB contain exactly one EFI application, correctly placed graphics without duplicate filenames, and no driver payload or build-only provenance; and
- publishing the exact CI-verified DEB and checksum, then comparing the complete downloaded release bytes before and after publication.

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

InfiltratorFS was not required for the successful Linux handoff. Boot no longer publishes or stages this experimental driver; Filesystems owns its qualification and release before independent installation.

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

Root VERSION identifies the development preview; inherited engine/profile versions retain their separate meaning. The current workflow verifies the GNU-EFI x86-64 Boot application independently of filesystem driver builds, then publishes only a Debian package and checksum. Its publisher downloads that artifact, verifies the checksum manifest, compares complete remote bytes before and after publication, and publishes the verified release bytes.

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

### 0.1.14 timing checkpoint fix

0.1.13 omitted timing marker 6, causing the menu's all-markers-valid gate to suppress the timing lines. 0.1.14 supplies that marker before tool discovery and guards its presence in CI. Use 0.1.14 or newer for physical timing tests; 0.1.13 cannot display the intended timing breakdown.

### 0.1.15 targeted filesystem-driver binding

The GNU-EFI broad controller path traverses the entire EFI handle database once for each device and invokes recursive controller connection, including unrelated hardware. This can be costly at startup and makes an unsafe assumption about handle index ordering within the nested database scan.

For Boot's normal, bundled filesystem drivers, the driver binding requirement is much narrower: connect to present logical partitions with BlockIo but without SimpleFileSystem. Boot 0.1.15 uses that subset after starting the bundled driver and preserves the legacy broad pass only when custom driver directories are explicitly configured or when the user requests a full interactive rescan.

Startup qualification: verify on actual firmware that Boot discovers the same internal ext4 Mint kernel and initrd, ESP GRUB fallback, Windows loader, and permitted removable volumes as 0.1.14. Inspect the RAM-only menu phase timings, particularly `drivers` and `re-scan`. The ext4 driver might require a firmware-specific fallback; keep GRUB as the default until confirmed. Do not infer a measured improvement solely from `systemd-analyze`'s loader interval, which also includes user menu time and handoff activities.

### Boot 0.1.16 filesystem extension contract

The 0.1.15 optimized binding pass excluded BlockIo handles with `LogicalPartition == FALSE`; this is too restrictive for future native-disk filesystem drivers including Amiga RDB volumes. 0.1.16 includes present whole-disk BlockIo handles while still avoiding unrelated firmware controllers. Drivers must perform their own Supported() probes and return unsupported promptly. Independently installed drivers are loaded before volume discovery. Driver selection and qualification belong to Filesystems/Setup; Boot packages no drivers.

Boot packages no filesystem drivers. Filesystems packages the independent ext4 EFI reader; the pinned InfiltratorFS adapter is retained there as migration source. Filesystems has an independent OFS EFI driver under qualification; complete end-to-end OFS boot is not yet established. Once an AmigaFS driver is available, qualify whole-disk RDB, common Amiga partitions, mixed GPT and RDB systems, unsupported random disk contents, read-only operation, predictable startup and failure isolation before enabling it in the release manifest.

## Boot 0.1.20 filesystem ownership boundary

All firmware filesystem implementation source and its vendor generator have
moved to Filesystems. Boot builds and packages its application independently,
rejecting any additional EFI binary or driver payload. Filesystems builds the
preserved ext4 reader and the independent OFS adapter without a Boot checkout.
The generic UEFI loader and normal read-only volume discovery remain in Boot.
Removing bundled drivers does not remove drivers independently installed on an
existing EFI partition. Fresh targets require the selected external modules.
Build/package validation of this move is not a new physical-firmware boot result.

## Boot 0.1.22 icon directory loading

The icon reader now opens the configured directory first and reads leaf
filenames through that handle. Empty and dot directory names retain the caller's
base directory; the former empty-directory path incorrectly acquired a leading
backslash and selected the volume root. Directory opens use the same bounded
transient-error retries as icon reads. The reader closes the directory handles
it opens and leaves the caller's base handle open.

The package stores OS, utility and volume icons once under `icons/`, with
`icons_dir icons` in its default configuration. Font atlases, banner and selection
graphics retain their existing configured locations beside the executable.

The host regression test compiles the production reader and image decoders,
loads a real PNG through mocked EFI protocols, and deliberately rejects combined
directory/file paths. It covers configured and custom directories, empty/dot
paths, fallback, missing files, transient directory/file failures, short reads
and handle ownership. This establishes behaviour under that failure model; the
exact cause of the original physical icon failure is not established by a mock.
Cold and warm physical-firmware qualification of the new directory layout
remains outstanding.
