# Validation

## Authority and scope

The authoritative automated verification path is [`.github/workflows/boot.yml`](../.github/workflows/boot.yml). It builds and tests the exact pushed revision and, on successful completion, publishes an AMD64 GitHub prerelease. A passed workflow is evidence for the named automated contracts, **not** certification for every physical firmware or operating system.

This repository does not use System Monitor's `make check` contract. Here, compilation is provided by `make gnuefi` and the dedicated executable regression scripts live in `tests/`. Test names should be read alongside their fixtures and assertions, not as broader promises.

## Current verification matrix

| Stage | Executable owner | What it establishes | What it does not establish |
| --- | --- | --- | --- |
| Repository/layout assertions | `.github/workflows/boot.yml` | Required source/build paths and key installer/driver call sites exist | Functional correctness of every call site |
| Standard GNU-EFI compilation | `Makefile` and workflow | EFI application and six required drivers compile into PE32+ artifacts | Bootability on arbitrary machines |
| Native CPU build profile | Workflow's native-build step | Selected x86-64 CPU-native flags produce the EFI application and drivers | Cross-machine CPU compatibility for the built binary |
| Release-package verification | `packaging/build-release.sh`, `packaging/test-release.sh` | Installed `.deb` payload and `postinst`, pinned native `.run` source, required drivers and SHA256SUMS | Native installation on a physical ESP |
| Disposable installation | `tests/install-safety.sh` | Fixed destination, six drivers, symlink rejection and preservation of Windows/GRUB/fallback | Every real partition layout or firmware behaviour |
| Firmware entry handling | `tests/nvram-safety.sh` | BOOT preference and refusal of duplicate/ambiguous records, including repeated install checks | Every OEM NVRAM implementation |
| UEFI launch smoke | `tests/ovmf-smoke.sh` | OVMF boots BOOT, which logs the full decoded byte count for its gzipped Btrfs driver, launches that driver and enters its main menu | Reading a Btrfs volume, starting a kernel or reaching OS userspace |

## Test setup and mechanics

### Native build

The top-level `Makefile` calls the required GNU-EFI library, BOOT executable and filesystem driver targets. The CI build verifies `boot/boot_x64.efi` and each of `ext2`, `ext4`, `reiserfs`, `iso9660`, `hfs` and `btrfs` in `drivers_x64/`. The workflow also exercises a CPU-native flag configuration in a disposable build environment. A successful compile provides **binary-format and link evidence**, not a runtime filesystem read proof.

### Release integrity

`packaging/build-release.sh` emits an installing generic AMD64 `.deb`, a native `.run` and `SHA256SUMS`. The `.run` pins its source revision and performs the CPU-native build followed by installation. `packaging/test-release.sh` validates the generic package's `postinst`, prebuilt payload and native installer; `tests/deb-install-safety.sh` exercises generic ESP copying and mocked NVRAM registration.

The `.deb` installs the generic prebuilt BOOT image, EFI drivers and resources under `/EFI/InfiltratorOS/BOOT` at package configuration; the `.run` builds on the target CPU and installs. Neither installation mode needs the previous BOOT executable to carry an ownership marker.

### Filesystem and installation safety

`tests/install-safety.sh` constructs disposable ESP directories containing synthetic Windows, GRUB and EFI fallback files. It executes the relevant installer functions against fixtures, checks that `BOOTX64.EFI` and six drivers appear under `/EFI/InfiltratorOS/BOOT`, and checks that the other bootloader files are unchanged.

Reinstallation deliberately removes stale contents **inside** BOOT's directory, including prior `boot.conf` and icon files. The test rejects alternate install paths and certain symbolic-link conditions. These fixture-based checks are not evidence that a production machine's firmware can load the resulting image.

### UEFI NVRAM

`tests/nvram-safety.sh` mocks `efibootmgr` and related host observations to test the BOOT firmware entry and boot ordering. It covers invalid boot order, duplicates, stale BOOT entries and inspection failures. The workflow additionally tests the BOOT default priority through a fresh installation and fifty repeat installations. Those checks defend the **NVRAM update algorithm**, not all vendor firmware implementations.

### UEFI menu

The fixture places BOOT and a gzip-compressed copy of `btrfs_x64.efi` on a FAT32 ESP inside a disposable GPT disk. It records the original driver's byte size before compression and enables `support_gzipped_loaders`. After booting OVMF, the script reads the UTF-16LE startup log from the disk. It checks for a successful decode with that exact byte count, the Btrfs driver launch, a zero program return and entry into the menu loop. A timeout is allowed because the menu can wait for input. Reading the disk log distinguishes that state from firmware hanging earlier. [OVMF fixture and assertions](../tests/ovmf-smoke.sh)

### Hosted FSW parser compilation

The test-only `filesystems/test/Makefile` builds `lsroot` and `lslr` from the same selected FSW backend (`ext2` by default). Its former XFS default and invalid dot-prefixed object reference were unbuildable because that backend source is absent. Run `make -C filesystems/test clean all` to check hosted compilation. Use `make -C filesystems/test clean all DRIVERNAME=ext4` to rebuild against EXT4. These programs require explicit disk-image fixtures for functional read testing; neither compilation nor printing directory entries demonstrates EFI firmware or Common V2 compatibility. The POSIX adapter must also release each directory dnode and close the image descriptor on both success and failure paths.

## Configuration regression contract

`boot.conf-sample` defines BOOT's current selection policy:

```ini
dont_scan_files +,grubx64.efi,EFI/BOOT/bootx64.efi
default_selection vmlinuz
```

This controls visible candidates and preference, not deletion or firmware registration. Changes to these lines must preserve automatic discovery on machines with differing operating-system counts and must not be described as a universal "two-icon" menu. Tests of actual menu filtering across multiple volumes are a separate requirement from the present OVMF startup smoke.

The sample includes disabled manual example stanzas. Their presence is not evidence of hard-coded active OS choices.

## Physical hardware qualification

Real-machine testing is required to establish outcomes the virtual tests cannot prove: volume discovery across different controller/partition layouts; the expected boot options; direct-kernel arguments and initrds; Secure Boot modes; graphics and input; hotplug/removable devices; vendor NVRAM behaviour; and recovery via alternative firmware boot entries.

A single successful direct Linux Mint boot on an EXT4 volume is an observed outcome, not a comprehensive compatibility matrix. Reports should distinguish successful menu discovery, EFI image launch, kernel handoff and complete OS startup.

## Common V2 qualification gap

At this revision, the released BOOT builds the original EFI filesystem drivers rather than a verified Common V2 `.so`-based firmware module loader. Full V2 compliance would require explicit ABI conformance tests, freestanding build checks, image authentication and dependency handling, module execution proofs and representative read-path tests for each filesystem. No such end-to-end guarantee is asserted by the current test suite.

## Failure reporting and documentation changes

Failed CI stops the current release pipeline and must be investigated from the failing step and log. Source-only comment removal previously required an update to a package-size assertion; removal of obsolete `COPYING.txt` later required deleting references from the workflow and release scripts. These are examples of repository-contract changes, not filesystem fixes.

Documentation should state **what passed**, **what failed**, and **what was not tested**. It must not infer success merely because a tag, artifact, EFI image or filesystem source file exists.


## Source-to-evidence traceability and outstanding regressions

The table distinguishes *existing fixture coverage* from *missing tests*. It does not claim these proposed assertions ran in CI.

| Contract or risk | Existing evidence | Missing regression evidence |
| --- | --- | --- |
| Fixed installation destination and preservation of other EFI vendors | `tests/install-safety.sh` uses a disposable ESP and checks three non-BOOT paths | Independent confirmation that the Linux reinstall removes only BOOT's dedicated directory and retains other vendors' files |
| Reinstallation without an ownership marker | `tests/install-safety.sh`, `tests/deb-install-safety.sh` | Both installers replace BOOT's fixed directory and reject symlink traversal without requiring old-file proof |
| Duplicate and ambiguous firmware entries | `tests/nvram-safety.sh`, workflow repeat-install checks | Physical firmware variable-write failure and partial-update recovery across different OEMs |
| Hosted FSW adapter resources | The test-only POSIX adapter closes its image descriptor on unmount or mount failure, releases directory dnodes and checks host `dirent` name capacity | Fault-injected allocation, malformed directory names and repeated mount/iteration leak checks |
| FSW mount on untrusted disk images | EXT2 geometry checks and EFI mount allocation checks are implemented | Fault-injected zero inode-group counts, allocation failures and truncated metadata to confirm the new rejection paths |
| FSW large-file reads | `filesystems/fsw_core.c` now preserves its full 64-bit stream position | Read at offsets below and above 4 GiB against independently generated reference bytes |
| EFI directory buffer negotiation | `filesystems/fsw_efi.c` now restores the cursor on conversion error | An `EFI_BUFFER_TOO_SMALL` retry must return the same entry rather than skipping one |
| EXT4 dirty filesystem | RECOVER is accepted by the current feature mask | Explicit tests of recovery-required images, plus a declared refusal or a validated read-only consistency policy |
| OS menu and direct Linux boot | `tests/ovmf-smoke.sh` observes driver loading and main-menu entry | Multi-volume discovery, duplicate suppression, direct kernel/initrd arguments and successful OS userspace on actual hardware |
| Decoder and filesystem metadata bounds | Btrfs UUID registration returns a distinct error on allocation failure; legacy NTFS/ISO/EFI parsing remains | Malformed-length and invalid-offset fixtures; injected Btrfs pool failure confirming FSW_OUT_OF_MEMORY |
| Network discovery and multi-volume tools | A network-volume allocation check is implemented | Verify launch failures, iPXE exit data, pool-failure handling and same-directory discovery on distinct volumes |
| Controller reconnect topology | `boot/driver_support.c` now requests the selected controller's index from its own handle snapshot | Synthetic databases with differing enumeration counts and orders to verify correct classification and stale-handle skipping |
| Secure Boot signing and key maintenance | Installer and healthcheck contain key generation, signing and enrollment operations | Fixture-driven status, passphrase, error and re-enrollment cases; firmware/shim validation on representative physical trust configurations |
| Common V2 filesystem ABI | No end-to-end BOOT V2 module validation in this revision | Exact V2 ABI conformance, shared implementation, freestanding build, authentication, load and representative read-path tests |

### Reproducible checks and their limits

From the repository root, `bash tests/install-safety.sh`, `bash tests/nvram-safety.sh`, `bash tests/deb-install-safety.sh` (with a built `.deb`), `bash packaging/test-release.sh` (with its expected release artifacts) and `bash tests/ovmf-smoke.sh` (with its QEMU, OVMF and prebuilt-image prerequisites) are the maintained entry points. `make gnuefi` compiles the firmware targets using the installed GNU-EFI toolchain. Some tests require setup or artifacts and should not be interpreted as executable on an arbitrary clean host without inspection of their scripts.

A review of source text is not a fresh execution of those commands. For a meaningful claim, record the tested commit SHA, environment, command, exit status, fixture inputs and observed artifact or log. Link each claim to the relevant executable assertion rather than relying on a passing badge or a file's existence.

### Comment-quality audit acceptance

A complete source-comment review checks each active, high-impact routine for verified preconditions, output and allocation ownership, integer/unit assumptions, error propagation and firmware side effects. Documentation must describe the current call order, including destructive steps, not the safer order intended by its authors. A remaining static hazard is a recorded limitation until its behaviour and regression tests are corrected. Comments must be dense in technical information, not blank-line padding or repetitive function-name paraphrases.
