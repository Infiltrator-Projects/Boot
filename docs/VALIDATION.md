# Validation

## Authority and scope

The authoritative automated verification path is [`.github/workflows/boot.yml`](../.github/workflows/boot.yml). It builds and tests the exact pushed revision and, if the release-publication step also succeeds, publishes an AMD64 GitHub prerelease. A passed workflow is evidence for the named automated contracts, **not** certification for every physical firmware or operating system.

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

## Recorded workflow evidence: 22 November 2026

[Run 44965981441](https://github.com/Infiltrator-Projects/Boot/actions/runs/44965981441) tested commit `bf1888a`. The GNU-EFI compilation, EFI binary check, native CPU build, `.run` and `.deb` package tests, source-isolated host regressions, disposable ESP installation, repeated firmware-entry ordering, OVMF main-menu smoke test and artifact upload all passed. **The workflow's overall conclusion was failure** because its **Publish GitHub prerelease** step failed. The prior passing checks must not be relabelled as a completed release.

The subsequent comment-only source revisions do not themselves supply additional physical hardware evidence, and a successful build cannot prove the behaviour of the millions of possible firmware/board/storage combinations. A regression run performed after these revisions is a new observation with its own commit SHA; do not reuse the earlier run as proof of a newly built artifact.

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

### Native source-isolated regressions

`bash tests/host-source-regressions.sh` extracts and compiles FindMem and Btrfs cleanup/statistics from the production source against minimal host stubs. The tests cover byte bounds, free-space arithmetic and one release per recovery-cache slot. They do not qualify a Btrfs disk image, EFI firmware or the intended Common V2 ABI.

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


## Source-comment review: reproducible acceptance criteria

This documentation correction is **not** an execution of the regressions below. It records the exact observations necessary to qualify the corresponding source contracts; it does not promote source inspection to test evidence.

| Source boundary | Reproduction needed | Acceptance condition |
| --- | --- | --- |
| `filesystems/fsw_efi.c:fsw_efi_read_block` | Exercise zero/oversized physical block geometry, near-`UINT64_MAX` block indices, last-sector partial read-ahead, wide-read failure and media/cache transitions | No wrapped byte offsets, out-of-window copy, stale cache data, or success after firmware I/O failure |
| `boot/scan.c:DuplicatesFallback` | Stub `EFI_FILE_INFO` allocation failure, very large reported files, one or both short reads, and changing file contents | No null dereference, out-of-bounds comparison or false positive equality; failures must not masquerade as proven identity |
| `boot/launch_efi.c:StartEFIImage` | Probe zero, truncated, malformed and expansion-heavy gzip inputs; LoadImage/StartImage authentication and failure paths; driver-versus-application unload | Decoded length never exceeds allocated capacity; child options remain live for the image handoff; handles and buffers are reclaimed according to image type and failure path |
| `boot/install.c:SetBootDefault` | BootOrder empty/odd-length, duplicate first element, selected entry absent, denied SetVariable and allocation failure | Invalid records do not write firmware; unique existing first entry avoids a rewrite; failures are reported and temporary buffers released |
| `boot/config.c:ReadFile` and `ReadLine` | File larger than `UINTN_MAX`, truncated successful read, UTF-8 multi-byte text and malformed UTF-16LE | No silent allocation truncation; tested distinction between advertised and consumed length; encoding limitations explicitly reported |
| `boot/driver_support.c:ScanDriverDir` | EFI candidate fails `LoadImage`, malformed PE image, denied Secure Boot image and a directory containing no usable drivers | Candidate count is not treated as successful filesystem driver binding; a readable post-scan volume is demonstrated separately |

Review requirements for the **remaining** code are independent of these cases: document firmware-owned versus project-owned allocations, input units and widths, integer overflow assumptions, stable-versus-borrowed pointers, protocol-open/close balance and whether an error code distinguishes absent input from an I/O failure. High comment density alone is not acceptance evidence. A complete postgraduate-level certification would require this examination across every relevant active source file plus executed tests of nontrivial contracts.

## Source-to-evidence traceability and outstanding regressions

The table distinguishes *existing fixture coverage* from *missing tests*. It does not claim these proposed assertions ran in CI.

| Contract or risk | Existing evidence | Missing regression evidence |
| --- | --- | --- |
| Fixed installation destination and preservation of other EFI vendors | `tests/install-safety.sh` exercises Linux reinstall against a disposable ESP and verifies three separate vendor files remain unchanged | Interrupted installation and real firmware recovery |
| Reinstallation without an ownership marker | `tests/install-safety.sh`, `tests/deb-install-safety.sh` verify fixed-directory replacement and symlink rejection without previous-file proof | Interrupted replacement between deletion and reinstall on a real ESP |
| Duplicate and ambiguous firmware entries | `tests/nvram-safety.sh`, workflow repeat-install checks, source-isolated `tests/host-bootorder-regressions.sh` | Physical firmware variable-write failure and partial-update recovery across different OEMs |
| Hosted FSW adapter resources | The test-only POSIX adapter closes its image descriptor on unmount or mount failure, releases directory dnodes and checks host `dirent` name capacity | Fault-injected allocation, malformed directory names and repeated mount/iteration leak checks |
| Pointer enumeration and screen allocation failure | Pointer protocol and handle arrays now compact together; missing protocol-array allocations disable that category; freed cursor images are nulled and the console header tolerates failed blank-row allocation | Firmware protocol mock with failed opens, pool-failure injection, repeated cleanup, and text-only boot under allocation pressure |
| Loader option substitution and textual GUID validation | `boot/mystrings.c` now assembles replacements transactionally, frees the old pool allocation only after success, checks output-size arithmetic and rejects non-hex GUID characters | EFI-hosted unit tests for replacement, escaped percent tokens, simulated pool allocation failure and malformed GUID strings |
| FSW mount on untrusted disk images | EXT2 geometry checks and EFI mount allocation checks are implemented | Fault-injected zero inode-group counts, allocation failures and truncated metadata to confirm the new rejection paths |
| Boot-signature scan bounds | `tests/host-source-regressions.sh` compiles FindMem and checks oversized, final-offset and empty needles | Real-device legacy detection |
| Btrfs memory ownership and capacity | `tests/host-source-regressions.sh` compiles production cleanup/stat routines against mock volumes | Actual Btrfs disk images and firmware allocator faults |
| Graphics allocation failure | The alpha compositor now returns without drawing when its image allocation fails | EFI fault-injection and graphical display replay |
| FSW large-file reads | `filesystems/fsw_core.c` now preserves its full 64-bit stream position | Read at offsets below and above 4 GiB against independently generated reference bytes |
| EFI directory buffer negotiation | `filesystems/fsw_efi.c` now restores the cursor on conversion error | An `EFI_BUFFER_TOO_SMALL` retry must return the same entry rather than skipping one |
| EXT4 dirty filesystem | RECOVER is accepted by the current feature mask | Explicit tests of recovery-required images, plus a declared refusal or a validated read-only consistency policy |
| OS menu and direct Linux boot | `tests/ovmf-smoke.sh` observes driver loading and main-menu entry | Multi-volume discovery, duplicate suppression, direct kernel/initrd arguments and successful OS userspace on actual hardware |
| Decoder and filesystem metadata bounds | Btrfs UUID registration reports allocation failure; NTFS attribute-name/index-entry spans and ISO SP marker bounds now have source guards | Malformed disk-image tests for NTFS attribute chains, compressed data, ISO continuation areas and EFI path decoding; injected Btrfs pool failure |
| Device-path text formatting | MyCatPrint uses transactional allocation and consistent CHAR16 length units; empty paths obtain a terminated string | Fault-injected allocation, oversized append and malformed node lengths. No trusted total byte length is available for the fallback DevicePathToStr walker |
| Network discovery and multi-volume tools | A network-volume allocation check is implemented | Verify launch failures, iPXE exit data, pool-failure handling and same-directory discovery on distinct volumes |
| Controller reconnect topology | `boot/driver_support.c` now requests the selected controller's index from its own handle snapshot | Synthetic databases with differing enumeration counts and orders to verify correct classification and stale-handle skipping |
| Secure Boot signing and key maintenance | Installer and healthcheck contain key generation, signing and enrollment operations | Fixture-driven status, passphrase, error and re-enrollment cases; firmware/shim validation on representative physical trust configurations |
| Common V2 filesystem ABI | No end-to-end BOOT V2 module validation in this revision | Exact V2 ABI conformance, shared implementation, freestanding build, authentication, load and representative read-path tests |
| Firmware menu boot-order mutation | Source-isolated `tests/host-bootorder-regressions.sh` checks unique-first no-op, duplicate cleanup, promotion, missing options, malformed variables and read/write failures | Real UEFI menu interactions, OOM injection into temporary menu allocations, NVRAM fault injection and partial-update recovery on physical firmware |

### Reproducible checks and their limits

From the repository root, `bash tests/host-source-regressions.sh`, `bash tests/host-bootorder-regressions.sh`, `bash tests/install-safety.sh`, `bash tests/nvram-safety.sh`, `bash tests/deb-install-safety.sh` (with a built `.deb`), `bash packaging/test-release.sh` (with its expected release artifacts) and `bash tests/ovmf-smoke.sh` (with its QEMU, OVMF and prebuilt-image prerequisites) are the maintained entry points. `make gnuefi` compiles the firmware targets using the installed GNU-EFI toolchain. Some tests require setup or artifacts and should not be interpreted as executable on an arbitrary clean host without inspection of their scripts.

A review of source text is not a fresh execution of those commands. For a meaningful claim, record the tested commit SHA, environment, command, exit status, fixture inputs and observed artifact or log. Link each claim to the relevant executable assertion rather than relying on a passing badge or a file's existence.

### Comment-only source equivalence

The documentation-only pass used `bf1888aae0f2f575a4997470a1745c39aee86da2` as its comparison base and examined the 28 changed maintained C files. A source comparison excluded C block/line comments outside string and character literals and normalised insignificant whitespace; the remaining code was identical for each modified implementation file. Comments were inserted at function boundaries or repositioned out of split declarations; no production expressions, preprocessor directives, installer logic, test assertions or ABI declarations were intentionally altered.

This establishes **non-comment source equivalence for the touched files**, not bit-for-bit equality of compiler outputs or completed validation on physical UEFI firmware. The inherited EFI drivers, additional source-only readers, vendor decoders, generated data and the entire filesystem module architecture retain their existing runtime behaviour.

### Comment-quality audit acceptance

A complete source-comment review checks each active, high-impact routine for verified preconditions, output and allocation ownership, integer/unit assumptions, error propagation and firmware side effects. Documentation must describe the current call order, including destructive steps, not the safer order intended by its authors. A remaining static hazard is a recorded limitation until its behaviour and regression tests are corrected. Comments must be dense in technical information, not blank-line padding or repetitive function-name paraphrases.

## Commentary review: specific unresolved proof obligations

This section records *review targets*, not newly executed tests or newly repaired runtime defects. Source comments now identify their assumptions so future assessments cannot mistake plausible prose for proven safety.

| Source contract | Source-visible assumption or failure distinction | Evidence still required |
| --- | --- | --- |
| `filesystems/fsw_ntfs.c` field readers and MFT initialisation | GETU* helpers do not take buffer lengths; `init_mft` does not propagate its allocation status | Truncated record fixtures, misaligned metadata where supported, and allocation-failure injection |
| `filesystems/fsw_ntfs.c` resident/nonresident reads | Short or zero return conflates EOF with selected failure paths; metadata-derived offset arithmetic needs independent bounds scrutiny | Sparse, resident, compressed, truncated and high-offset image fixtures with independently checked output |
| `filesystems/fsw_ntfs.c` index allocation bitmap and reparse links | Bitmap last-byte comparison and reparse-field bounds require adversarial record coverage | Zero, first, last, and one-past-end index cases; malformed reparse tags and short name buffers |
| `EfiLib/DevicePath.c` diagnostic renderers | Node renderers assume sufficient backing data; fallback walk lacks an external total byte length | Malformed node-length, missing terminator, display truncation and allocator-fault fixtures |
| `boot/scan.c` inventory and submenu lifetime | Menu children can share volume pointers; network discovery may execute an EFI child while scanning | Repeated rescans, failed child launches, menu ownership assertions and multi-volume selection checks |
| `boot/lib.c` device-path to volume resolution | Existing output pointers are released before replacement, although discovered volume pointers may be borrowed from the global inventory | Ownership-aware call-site review and fault injection; no claim of safety from comment-only work |

**Source-only change check.** For an engineering-comments commit, compare the changed C and header files with comments removed and whitespace normalised; the remaining tokens must match the parent revision. Also review every added assertion against its implementing routine. This equivalence test excludes documentation itself and is not a substitute for firmware compilation, host unit tests, QEMU execution, or a physical-hardware boot test.
