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
| Release-package verification | `packaging/build-release.sh`, `packaging/test-release.sh` | Staged `.deb` contents, pinned `.run` source metadata, required files and SHA256SUMS | Native installation on a physical ESP |
| Disposable installation | `tests/install-safety.sh` | Owned destination, six drivers, checksum marker, rejection of unsafe paths, preservation of Windows/GRUB/fallback | Every real partition layout or firmware behaviour |
| Firmware entry handling | `tests/nvram-safety.sh` | BOOT preference and refusal of duplicate/ambiguous records, including repeated install checks | Every OEM NVRAM implementation |
| UEFI launch smoke | `tests/ovmf-smoke.sh` | OVMF boots BOOT, which logs the full decoded byte count for its gzipped Btrfs driver, launches that driver and enters its main menu | Reading a Btrfs volume, starting a kernel or reaching OS userspace |

## Test setup and mechanics

### Native build

The top-level `Makefile` calls the required GNU-EFI library, BOOT executable and filesystem driver targets. The CI build verifies `boot/boot_x64.efi` and each of `ext2`, `ext4`, `reiserfs`, `iso9660`, `hfs` and `btrfs` in `drivers_x64/`. The workflow also exercises a CPU-native flag configuration in a disposable build environment. A successful compile provides **binary-format and link evidence**, not a runtime filesystem read proof.

### Release integrity

`packaging/build-release.sh` emits a generic AMD64 `.deb`, a single native `.run` and `SHA256SUMS`. The `.run` contains the verified source commit and version substituted into the maintained installer. `packaging/test-release.sh` checks that the package stages BOOT and the six drivers and has no unexpected firmware-installation maintainer hooks. It checks the installer is one program with the expected destination and without the disallowed build-only path.

The `.deb` stages files; the `.run` compiles **and installs**. The distinction is tested and must remain explicit in release notes.

### Filesystem and installation safety

`tests/install-safety.sh` constructs disposable ESP directories containing synthetic Windows, GRUB and EFI fallback files. It executes the relevant installer functions against fixtures, checks that the BOOT-owned `BOOTX64.EFI` and six drivers appear under `/EFI/InfiltratorOS/BOOT`, verifies the ownership checksum, and checks that the other bootloader files are unchanged.

Reinstallation deliberately removes stale contents **inside** BOOT's directory, including prior `boot.conf` and icon files. The test rejects alternate install paths and certain symbolic-link conditions. These fixture-based checks are not evidence that a production machine's firmware can load the resulting image.

### UEFI NVRAM

`tests/nvram-safety.sh` mocks `efibootmgr` and related host observations to test the BOOT firmware entry and boot ordering. It covers invalid boot order, duplicates, stale BOOT entries and inspection failures. The workflow additionally tests the BOOT default priority through a fresh installation and fifty repeat installations. Those checks defend the **NVRAM update algorithm**, not all vendor firmware implementations.

### UEFI menu

The fixture places BOOT and a gzip-compressed copy of `btrfs_x64.efi` on a FAT32 ESP inside a disposable GPT disk. It records the original driver's byte size before compression and enables `support_gzipped_loaders`. After booting OVMF, the script reads the UTF-16LE startup log from the disk. It checks for a successful decode with that exact byte count, the Btrfs driver launch, a zero program return and entry into the menu loop. A timeout is allowed because the menu can wait for input. Reading the disk log distinguishes that state from firmware hanging earlier. [OVMF fixture and assertions](../tests/ovmf-smoke.sh)

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
