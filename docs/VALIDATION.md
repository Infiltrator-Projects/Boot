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

## Interpretation of source-level contracts

Code comments document the branch-level contract **as implemented**; they do not establish that the contract has been exercised on firmware. In particular:

| Interface | Source-visible fact | Independent evidence needed |
| --- | --- | --- |
| `FindBootOrderEntries` | Malformed or missing `Boot####` entries are skipped; memory exhaustion may truncate the returned list | Inject malformed UTF-16 descriptions, inconsistent path lengths and allocation failures; verify callers do not treat partial results as complete firmware state |
| `DeleteInvalidBootEntries` | Non-`EFI_NOT_FOUND` reads abort cleanup; a successful `Boot####` deletion can precede failed `BootOrder` cleanup | Inject NVRAM read/write errors and verify final variables as well as surfaced status |
| `ConnectFilesystemDriver` | Disconnect/reconnect outcomes are not returned to callers | Firmware mocks and OVMF tests showing bindings before and after connection, including failed disconnect/reconnect |
| `LoadDrivers` | Positive return counts candidate driver filenames, not mounts | Distinguish candidate, authenticated image, successful binding, volume visibility and successful file read in separate assertions |
| `fsw_efi_DriverBinding_Start/Stop` | Volume storage contains an installed protocol; teardown must uninstall it before release | Fault-inject mount, protocol installation and uninstallation, followed by protocol use/cleanup checks |

These are **unverified regression requirements**, not new test results. The descriptions also expose areas where a public API cannot express partial failure (for example, the `VOID` controller reconnection function). Documentation must preserve this limitation rather than silently implying stronger success guarantees.

### Additional source-level regression obligations

| Routine | Failure or ambiguity visible in source | Missing proof |
| --- | --- | --- |
| `GenerateWaitList` | Unchecked allocation and replacement of an existing global wait-list pointer | Force allocation failure and repeated construction; check event-array lifetime |
| `WaitForInput` | The zero-timeout path excludes the timer slot yet interprets the last included slot as a timeout | Simulate keyboard and last-pointer events with zero timeout; verify event classification and event creation/close behaviour |
| `IsSymbolicLink` | Successful Open retains a file handle; failed metadata observation is treated as zero length | Instrument repeated directory scans and injected Open/LibFileInfo failures |
| `GetFirstOptionsFromFile` | REFIT_FILE wrapper is freed without an explicit free of its data buffer | Trace each returned REFIT_FILE allocation and the release of all associated payload ownership |
| `EfivarGetRaw` | Disk read failure after directory creation does not retry NVRAM | Mock local-variable read failures separately from directory-open failures |

All entries are review observations and proposed regressions, not evidence that those tests exist or pass.

### Loader and cache ownership regressions

The source review also establishes **test requirements, not passing tests**, for: a failed `StoreLoaderName` firmware-variable write and its undetectable VOID return; null options combined with a discovered initrd in `AddInitrdToOptions`; repeated cache pin/release and volume teardown with outstanding pins in `fsw_block_get`; and near-overflow cache-size arithmetic on 32-bit widths. A regression should inject each failure at its production boundary rather than infer correctness from an isolated utility test.

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

## Failure-semantics matrix: predicates must not overclaim

| Routine | Positive or success-like result | Ambiguity or known defect | Evidence still absent |
| --- | --- | --- | --- |
| `IsValidLoader` | EFI-looking header, gzip magic, or the current NULL-argument EFI default | NULL-input path returns EFI classification without a file read; early probe-buffer leaks; PE offset is accessed after potentially short read | Short read, NULL-input, failed-Open and allocator fault injection with leak checks |
| `DuplicatesFallback` | Reported byte comparison against a separately opened fallback | Actual byte counts are not independently checked, and metadata/allocations depend on mutable disk state | Two independently shortened reads, huge EFI_FILE_INFO sizes, mutation between opens and allocation failures |
| `ScanDriverDir` | A driver candidate was found in a directory | Does not establish `StartEFIImage`, driver binding or a readable filesystem | Driver load/bind failures and actual post-rescan file access |
| `fsw_efi_read_block` | Directly returned EFI status in an FSW-declared return type | Status namespaces are not proven equivalent; arithmetic and cache spans are not bounded | Overflow, cache revalidation, firmware read failure and status mapping fixtures |

This matrix documents source-observable behaviour and explicit test obligations;
no row is evidence that its proposed fault-injection test has executed.

## Specific verification gaps exposed by source review

The following are **not tests already passed**. They are distinct
verification obligations derived from observed control flow:

| Source | Observable issue or contract | Missing executable proof |
| --- | --- | --- |
| `boot/launch_efi.c:IsValidLoader` | Early return after probe-buffer allocation on missing arguments or failed file open | Allocation-count/leak assertion for each early exit |
| `boot/launch_efi.c:IsValidLoader` | PE offset examined before establishing full read length | Short-read and malformed-offset tests with initialisation checking |
| `boot/install.c:FindBootOrderEntries` | Malformed records are skipped; allocation failure can return a valid prefix | EFI-variable fixture matrix distinguishing partial result from complete enumeration |
| `boot/install.c:DeleteInvalidBootEntries` | BootOrder rewrite follows inspection of entries; failures other than NOT_FOUND abort | Fault injection before and after individual Boot#### reads, plus post-write state inspection |
| `filesystems/fsw_efi.c:fsw_efi_DriverBinding_Start/Stop` | Claimed protocol and mount resources have different teardown obligations | EFI protocol-mock failure injection at mount, interface install, uninstall and close |

A comment-only revision does not resolve these implementation risks.
Source-level explanations provide a reviewable contract; passing a build or
a previous OVMF menu smoke test does not satisfy the above fault injections.

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

## 22 November 2026 documentation-only contract corrections

This record maps the latest source-comment clarifications to observable behaviours.
No runtime C statements, tests, packaging, or EFI binaries were changed by
these corrections. The static source-equivalence check strips C comments and
compares the remaining non-whitespace text before each update; it is not a
compiler, ABI, or firmware execution test.

| Source | Precisely documented boundary | Required executable counterexample or acceptance evidence |
| --- | --- | --- |
| `filesystems/fsw_ntfs.c:test_idxbmp` | One-based block is decremented; `block > bmpsz` allows a byte index equal to byte length | Bitmap with zero, first, last valid and first invalid block; guard-page/ASan execution against short bitmap |
| `filesystems/fsw_ext2.c` | File logical indexes and disk physical blocks differ across each indirection level | Reference images that cross direct/single/double/triple-indirect boundaries, include malformed pointers, and compare extracted bytes |
| `filesystems/fsw_ext4.c` | Extent-leaf/index geometry is untrusted; RECOVER acceptance does not replay journal | Depth/length and out-of-range extent fixtures plus a recovery-required volume policy test |
| `filesystems/fsw_btrfs.c` | Extent cache key spans tree, inode and range; chunk translation and decompression have separate lifetimes | Two subvolumes with equal inode number, compressed/uncompressed extents, malformed lengths and missing/RAID devices |
| `boot/scan.c:FindTool` | Directory-name deduplication crosses volume iterations without volume identity | Two distinct readable volumes containing the same tool directory; both eligible tools must be considered |
| `boot/config.c:GetFirstOptionsFromFile` | `REFIT_FILE` wrapper is freed without a visible release of its `Buffer` allocation | Instrument allocation/free accounting over present, missing, empty and malformed option files |

A known source defect is not an acceptable test result merely because a comment
describes it. The checks in the final column remain **unexecuted requirements** in
this documentation-only change, and the first observed failure must remain a
reported failure until corrected in production code and retested. In particular,
Common V2 BOOT loading is neither built nor qualified by these comment changes.

## Source-file documentation review ledger — 22 November 2026

This is a deliberately bounded record, not a claim that every tracked
repository object now meets a master's-level documentation standard. The
repository inventory at the start of this pass contained **363 actual files**
(208 text/source/configuration files and 155 binary or other resource files).
The 155 nontext assets require asset provenance and integrity inspection,
rather than artificial inline source comments. The text-file inventory was
screened for role, existing top-level commentary and build/data ownership;
screening is not a full proof of each function or decoder. The files below
received source-comment corrections after inspection of their existing
contracts. Some other source files were inspected and left unchanged because
their current interface documentation already explained their purpose or
because they are derived/vendor-owned implementation data.

| File changed | Contract clarified | Evidence boundary |
| --- | --- | --- |
| `boot/apple.c` | CSR read/display/write boundaries | Comment-only change; runtime regression not executed |
| `boot/gpt.c` | CRC versus trust; cache and copied partition lifetime | Comment-only change; runtime regression not executed |
| `boot/launch_legacy.c` | EBR offset origins and CSM-dependent launch | Comment-only change; runtime regression not executed |
| `boot/log.c` | Persistent logging side effects | Comment-only change; runtime regression not executed |
| `boot/pointer.c` | Pointer, cursor snapshot and redraw lifetimes | Comment-only change; runtime regression not executed |
| `boot/line_edit.c` | Cursor versus viewport coordinate contract | Comment-only change; runtime regression not executed |
| `boot/crc32.c` | CRC convention versus authenticity | Comment-only change; runtime regression not executed |
| `boot/icns.c` | Shared icon-cache ownership | Comment-only change; runtime regression not executed |
| `boot/linux.c` | Kernel/initrd/options selection versus OS startup | Comment-only change; runtime regression not executed |
| `boot/main.c` | Scan invalidation and returning EFI image lifecycle | Comment-only change; runtime regression not executed |
| `boot/launch_efi.c` | LoadImage/StartImage/trust-stage separation | Comment-only change; runtime regression not executed |
| `boot/driver_support.c` | Driver filename count versus successful mount | Comment-only change; runtime regression not executed |
| `boot/menu.c` | Selection inventory validity and render-only state | Comment-only change; runtime regression not executed |
| `boot/screen.c` | Scratch surface, mode and console dimension lifetimes | Comment-only change; runtime regression not executed |
| `boot/lib.c` | Firmware borrowed handles versus owned pool allocations | Comment-only change; runtime regression not executed |
| `boot/mystrings.c` | ASCII-like folding and string allocation identity | Comment-only change; runtime regression not executed |
| `filesystems/fsw_core.c` | Physical/logical units and stream/cache independence | Comment-only change; runtime regression not executed |
| `filesystems/fsw_efi.c` | EFI-to-FSW status mismatch and cache range limits | Comment-only change; runtime regression not executed |
| `filesystems/fsw_hfs.c` | B-tree node/record bounds and name storage | Comment-only change; runtime regression not executed |
| `filesystems/fsw_iso9660.c` | SUSP/Rock Ridge continuation bounds | Comment-only change; runtime regression not executed |
| `filesystems/fsw_reiserfs.c` | Key versus item bounds and read-only traversal | Comment-only change; runtime regression not executed |
| `EfiLib/BdsConnect.c` | Handle snapshot invalidation on driver dispatch | Comment-only change; runtime regression not executed |
| `EfiLib/BdsHelper.c` | BBS firmware-dependent execution | Comment-only change; runtime regression not executed |
| `EfiLib/BdsTianoCore.c` | UEFI in/out byte lengths and ABI compatibility | Comment-only change; runtime regression not executed |
| `libeg/image.c` | Deep/shallow image data ownership | Comment-only change; runtime regression not executed |
| `libeg/screen.c` | GOP mode changes versus discovered menu state | Comment-only change; runtime regression not executed |
| `libeg/ttf.c` | Package checksums versus runtime table checks/rasterisation | Comment-only change; runtime regression not executed |
| `libeg/load_bmp.c` | BMP stride/origin/length distinction | Comment-only change; runtime regression not executed |
| `libeg/load_icns.c` | RLE input and output bounds | Comment-only change; runtime regression not executed |
| `mok/security_policy.c` | Independent Security/Security2 hook restoration | Comment-only change; runtime regression not executed |
| `mok/simple_file.c` | EFI file handle versus read buffer ownership | Comment-only change; runtime regression not executed |
| `gzip/decompress_inflate.c` | Gzip recognition/decompression/load distinction | Comment-only change; runtime regression not executed |
| `EfiLib/BmLib.c` | Nontransactional realloc and lost allocation distinction | Comment-only change; runtime regression not executed |
| `EfiLib/legacy.c` | BBS snapshot index and firmware variable mutation | Comment-only change; runtime regression not executed |
| `filesystems/fsw_btrfs.c` | Inode body size, LZO page bounds and compression dispatch index assumptions | Comment-only change; malformed-image regression not executed |
| `filesystems/fsw_ntfs.c` | Runlist offsets, MFT-map partial completion and child-name buffer bounds | Comment-only change; malformed-image regression not executed |

The source changes above were individually compared with their fetched
pre-edit text after removing C block comments and whitespace. That check
preserved the remaining executable text for each file. It is a static
equivalence check, **not** a GNU-EFI build, binary comparison, EFI protocol
exercise, malformed-image fuzz test or physical firmware validation.

The accompanying `docs/ARCHITECTURE.md` change corrects a stale reference
to nonexistent `libeg/text.c` (the actual renderer is `libeg/ttf.c`) and
separates package-time checksums, runtime TrueType table checks, offscreen
glyph probes and physical display fidelity. Markdown relative-file links in
the maintained documentation were checked against the repository tree;
no broken relative Markdown links were found in that screen.

**Completion criteria still outstanding:** full function-level verification
of all maintained executable files; reproducible regression evidence for
the identified safety assumptions; independent qualification of the
currently shipped binary/asset inventory; and physical firmware results.
Generated headers, bundled vendor decompression/parser bodies and certificate
bytes must be assessed at their integration boundaries without adding
decorative comments or modifying their provenance.

**Concurrent modification boundary.** Other commits landed on `main` during
this review, including font renderer and OVMF font-test changes. The comment
preservation comparisons apply only to the individual updates made during this
review, not to the entire difference between the initial and final branch
heads. A full repository binary/source equivalence claim would therefore be
incorrect without separating those other commits. This is a **partial**
file-by-file review, not a completed repository-wide certification.

## Additional function-level findings — current comment pass

Each row identifies an observed source expression and an executable regression
that is **not** evidenced by the documentation-only edits. These are explicit
implementation gaps rather than completed safety fixes.

| Source expression | Observed limitation | Required regression |
| --- | --- | --- |
| `libeg/image.c:egLoadIcon` | The result of `egDecodeAny` is dereferenced before it is checked for NULL | Invalid/unsupported icon bytes and allocator failure must return without dereference |
| `libeg/image.c:egCropImage` | `StartX + Width` and `StartY + Height` can wrap before boundary comparison | Unsigned-max coordinates and zero/nonzero crop widths must be rejected safely |
| `libeg/screen.c:egScreenShot` | Failure of `egFindESP` returns after BMP encoding without freeing `FileData` | Failed ESP lookup with tracked EFI pool allocation must release encoded image bytes |
| `filesystems/fsw_ext4.c:test_root` | The power loop multiplies a fixed-width accumulator without overflow detection | Large group numbers around the first 32-bit wrap must terminate and report correct sparse-group membership |
| `filesystems/fsw_efi.c:fsw_efi_main` | If Component Name protocol installation fails after Driver Binding was installed, the first protocol is not visibly rolled back | Force the second protocol install to fail and verify all previously published interfaces are removed or deliberately retained under a documented policy |
| `boot/menu.c:TextMenuStyle` | Display-row string allocation is followed by dereferences without explicit independent OOM checks | Inject OOM at the array and each row allocation before painting and during menu teardown |
| `boot/menu.c:ComputeSubScreenWindowSize` | Width calculation mixes rendered-pixel text measurements with UTF-16 entry title code-unit counts | Compare large proportional TrueType menu labels at narrow and wide GOP modes with clipping detection |
| `libeg/ttf.c:mbPrepare` | The initialisation latch is set before all font files load, so later SelfDir availability does not trigger a retry | Initialise with unavailable SelfDir, then restore it and verify documented recovery/retry policy |
| `filesystems/fsw_btrfs.c:grub_btrfs_lzo_decompress` | Input length and intermediate block-boundary checks are insufficient for all page header reads | Truncated page headers, crafted compressed lengths and output-boundary fault injection |
| `filesystems/fsw_ntfs.c:attribute_get_rle` | No minimum record length and no validation of the encoded runlist start offset | Short nonresident attributes and offsets beyond record length must not reach decoding |

These checks are independent of whether an unrelated workflow succeeds at
compilation or displays a boot menu. They also remain distinct from the planned
Common V2-only filesystem ABI work.

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

## Source-function inventory and inspection boundary — 22 November 2026

Baseline revision: `3723692da9de44ab97082e8d6df7197e612a8e76`. The following is a **static function-definition inventory** of every tracked C/header file, including generated and bundled code. A function count is determined from source-like definitions (and can include conditional build alternatives or miss unusual macro signatures); it is *not* a test-pass count. Files are classified by code ownership to separate direct review of BOOT-owned logic from upstream decoder and ABI/derived-data integration review. Source commentary changes do not certify behaviour. Specific uncorrected defects are recorded in the proof-obligation tables above.

| File | Function-like definitions | Review scope |
| --- | ---: | --- |
| `EfiLib/AbsolutePointer.h` | 0 | ABI/header |
| `EfiLib/BdsConnect.c` | 3 | Maintained implementation |
| `EfiLib/BdsHelper.c` | 2 | Maintained implementation |
| `EfiLib/BdsHelper.h` | 0 | ABI/header |
| `EfiLib/BdsTianoCore.c` | 2 | Maintained implementation |
| `EfiLib/BmLib.c` | 4 | Maintained implementation |
| `EfiLib/DevicePath.c` | 37 | Maintained implementation |
| `EfiLib/DevicePathUtilities.h` | 0 | ABI/header |
| `EfiLib/GenericBdsLib.h` | 0 | ABI/header |
| `EfiLib/LegacyBios.h` | 0 | ABI/header |
| `EfiLib/Platform.h` | 0 | ABI/header |
| `EfiLib/gnuefi-helper.c` | 1 | Maintained implementation |
| `EfiLib/gnuefi-helper.h` | 0 | ABI/header |
| `EfiLib/legacy.c` | 13 | Maintained implementation |
| `EfiLib/legacy.h` | 0 | ABI/header |
| `boot/AutoGen.c` | 1 | Generated/embedded |
| `boot/AutoGen.h` | 0 | Generated/embedded |
| `boot/apple.c` | 4 | Maintained implementation |
| `boot/apple.h` | 0 | ABI/header |
| `boot/config.c` | 22 | Maintained implementation |
| `boot/config.h` | 0 | ABI/header |
| `boot/crc32.c` | 1 | Maintained implementation |
| `boot/crc32.h` | 0 | ABI/header |
| `boot/driver_support.c` | 5 | Maintained implementation |
| `boot/driver_support.h` | 0 | ABI/header |
| `boot/global.h` | 0 | ABI/header |
| `boot/gpt.c` | 7 | Maintained implementation |
| `boot/gpt.h` | 0 | ABI/header |
| `boot/icns.c` | 3 | Maintained implementation |
| `boot/icns.h` | 0 | ABI/header |
| `boot/install.c` | 8 | Maintained implementation |
| `boot/install.h` | 0 | ABI/header |
| `boot/launch_efi.c` | 8 | Maintained implementation |
| `boot/launch_efi.h` | 0 | ABI/header |
| `boot/launch_legacy.c` | 15 | Maintained implementation |
| `boot/launch_legacy.h` | 0 | ABI/header |
| `boot/lib.c` | 49 | Maintained implementation |
| `boot/lib.h` | 0 | ABI/header |
| `boot/line_edit.c` | 3 | Maintained implementation |
| `boot/line_edit.h` | 0 | ABI/header |
| `boot/linux.c` | 7 | Maintained implementation |
| `boot/linux.h` | 0 | ABI/header |
| `boot/log.c` | 5 | Maintained implementation |
| `boot/log.h` | 0 | ABI/header |
| `boot/main.c` | 10 | Maintained implementation |
| `boot/menu.c` | 43 | Maintained implementation |
| `boot/menu.h` | 0 | ABI/header |
| `boot/mystrings.c` | 21 | Maintained implementation |
| `boot/mystrings.h` | 0 | ABI/header |
| `boot/pointer.c` | 9 | Maintained implementation |
| `boot/pointer.h` | 0 | ABI/header |
| `boot/scan.c` | 31 | Maintained implementation |
| `boot/scan.h` | 0 | ABI/header |
| `boot/screen.c` | 22 | Maintained implementation |
| `boot/screen.h` | 0 | ABI/header |
| `filesystems/AutoGen.c` | 5 | Generated/embedded |
| `filesystems/AutoGen.h` | 0 | Generated/embedded |
| `filesystems/crc32c.c` | 1 | Maintained implementation |
| `filesystems/edk2/ComponentName.h` | 0 | ABI/header |
| `filesystems/edk2/DriverBinding.h` | 0 | ABI/header |
| `filesystems/fsw_base.h` | 0 | ABI/header |
| `filesystems/fsw_btrfs.c` | 39 | Maintained implementation |
| `filesystems/fsw_btrfs_zstd.h` | 5 | ABI/header |
| `filesystems/fsw_core.c` | 25 | Maintained implementation |
| `filesystems/fsw_core.h` | 0 | ABI/header |
| `filesystems/fsw_efi.c` | 33 | Maintained implementation |
| `filesystems/fsw_efi.h` | 0 | ABI/header |
| `filesystems/fsw_efi_base.h` | 0 | ABI/header |
| `filesystems/fsw_efi_edk2_base.h` | 0 | ABI/header |
| `filesystems/fsw_efi_lib.c` | 3 | Maintained implementation |
| `filesystems/fsw_ext2.c` | 11 | Maintained implementation |
| `filesystems/fsw_ext2.h` | 0 | ABI/header |
| `filesystems/fsw_ext2_disk.h` | 0 | ABI/header |
| `filesystems/fsw_ext4.c` | 16 | Maintained implementation |
| `filesystems/fsw_ext4.h` | 0 | ABI/header |
| `filesystems/fsw_ext4_disk.h` | 0 | ABI/header |
| `filesystems/fsw_hfs.c` | 26 | Maintained implementation |
| `filesystems/fsw_hfs.h` | 8 | ABI/header |
| `filesystems/fsw_iso9660.c` | 14 | Maintained implementation |
| `filesystems/fsw_iso9660.h` | 0 | ABI/header |
| `filesystems/fsw_lib.c` | 8 | Maintained implementation |
| `filesystems/fsw_ntfs.c` | 57 | Maintained implementation |
| `filesystems/fsw_reiserfs.c` | 14 | Maintained implementation |
| `filesystems/fsw_reiserfs.h` | 0 | ABI/header |
| `filesystems/fsw_reiserfs_disk.h` | 0 | ABI/header |
| `filesystems/fsw_strfunc.h` | 15 | ABI/header |
| `filesystems/gzio.c` | 13 | Bundled decoder |
| `filesystems/hfs_format.h` | 0 | ABI/header |
| `filesystems/lzoconf.h` | 0 | ABI/header |
| `filesystems/lzodefs.h` | 0 | Bundled decoder |
| `filesystems/minilzo.c` | 11 | Bundled decoder |
| `filesystems/minilzo.h` | 0 | Bundled decoder |
| `filesystems/scandisk.c` | 5 | Maintained implementation |
| `filesystems/test/fsw_posix.c` | 13 | Maintained implementation |
| `filesystems/test/fsw_posix.h` | 0 | ABI/header |
| `filesystems/test/fsw_posix_base.h` | 0 | ABI/header |
| `filesystems/test/lslr.c` | 3 | Maintained implementation |
| `filesystems/test/lsroot.c` | 1 | Maintained implementation |
| `filesystems/zstd/bitstream.h` | 12 | Bundled decoder |
| `filesystems/zstd/error_private.h` | 0 | Bundled decoder |
| `filesystems/zstd/fse.h` | 6 | Bundled decoder |
| `filesystems/zstd/fse_decompress.c` | 6 | Bundled decoder |
| `filesystems/zstd/huf.h` | 0 | Bundled decoder |
| `filesystems/zstd/huf_decompress.c` | 22 | Bundled decoder |
| `filesystems/zstd/mem.h` | 8 | Bundled decoder |
| `filesystems/zstd/xxhash.h` | 0 | Bundled decoder |
| `filesystems/zstd/xxhash64.c` | 5 | Bundled decoder |
| `filesystems/zstd/zstd.h` | 2 | Bundled decoder |
| `filesystems/zstd/zstd_decompress.c` | 39 | Bundled decoder |
| `filesystems/zstd/zstd_internal.h` | 3 | Bundled decoder |
| `gzip/decompress_inflate.c` | 2 | Maintained implementation |
| `gzip/zlib_inflate/inffast.c` | 2 | Bundled decoder |
| `gzip/zlib_inflate/inffast.h` | 0 | Bundled decoder |
| `gzip/zlib_inflate/inffixed.h` | 0 | Bundled decoder |
| `gzip/zlib_inflate/inflate.c` | 9 | Bundled decoder |
| `gzip/zlib_inflate/inflate.h` | 0 | Bundled decoder |
| `gzip/zlib_inflate/inftrees.c` | 1 | Bundled decoder |
| `gzip/zlib_inflate/inftrees.h` | 0 | Bundled decoder |
| `gzip/zlib_inflate/infutil.c` | 1 | Bundled decoder |
| `gzip/zlib_inflate/infutil.h` | 0 | Bundled decoder |
| `gzip/zlib_inflate/zconf.h` | 0 | Bundled decoder |
| `gzip/zlib_inflate/zlib.h` | 1 | Bundled decoder |
| `gzip/zlib_inflate/zutil.h` | 1 | Bundled decoder |
| `include/Bmp.h` | 0 | ABI/header |
| `include/Handle.h` | 0 | ABI/header |
| `include/PeImage.h` | 0 | ABI/header |
| `include/PeImage2.h` | 0 | ABI/header |
| `include/RemovableMedia.h` | 0 | ABI/header |
| `include/egemb_arrow_left.h` | 0 | Generated/embedded |
| `include/egemb_arrow_right.h` | 0 | Generated/embedded |
| `include/egemb_back_selected_big.h` | 0 | Generated/embedded |
| `include/egemb_back_selected_small.h` | 0 | Generated/embedded |
| `include/egemb_boot_banner.h` | 0 | Generated/embedded |
| `include/refit_call_wrapper.h` | 0 | ABI/header |
| `include/syslinux_mbr.h` | 0 | Generated/embedded |
| `include/tiano_includes.h` | 0 | ABI/header |
| `include/version.h` | 0 | ABI/header |
| `libeg/efiConsoleControl.h` | 0 | ABI/header |
| `libeg/efiUgaDraw.h` | 0 | ABI/header |
| `libeg/image.c` | 24 | Maintained implementation |
| `libeg/libeg.h` | 0 | ABI/header |
| `libeg/libegint.h` | 0 | ABI/header |
| `libeg/load_bmp.c` | 2 | Maintained implementation |
| `libeg/load_icns.c` | 2 | Maintained implementation |
| `libeg/lodepng.c` | 254 | Bundled decoder |
| `libeg/lodepng.h` | 0 | ABI/header |
| `libeg/lodepng_xtra.c` | 8 | Maintained implementation |
| `libeg/nanojpeg.c` | 31 | Bundled decoder |
| `libeg/nanojpeg_xtra.c` | 1 | Maintained implementation |
| `libeg/screen.c` | 19 | Maintained implementation |
| `libeg/stb_truetype.h` | 151 | Bundled decoder |
| `libeg/ttf.c` | 34 | Maintained implementation |
| `mok/guid.c` | 0 | Maintained implementation |
| `mok/guid.h` | 0 | ABI/header |
| `mok/mok.c` | 3 | Maintained implementation |
| `mok/mok.h` | 0 | ABI/header |
| `mok/security_policy.c` | 4 | Maintained implementation |
| `mok/security_policy.h` | 0 | ABI/header |
| `mok/simple_file.c` | 2 | Maintained implementation |
| `mok/simple_file.h` | 0 | ABI/header |
| `tests/icon-detection-regressions.c` | 23 | Maintained implementation |
| `tests/validate-fonts.c` | 2 | Maintained implementation |
**Snapshot composition:** 366 tracked files, comprising 162 C/header files, 50 other text/configuration files and 154 nontext assets. C/header entries above are source-like function-definition *inventory*, not claims of runtime conformance. Generated byte arrays are not ordinary application functions.

## Non-C tracked text and build/test review inventory

The remaining tracked text files are assessed for deployment, build, test,
provenance or documentation semantics. Shell function counts are lexical
entry-point counts, not passed test counts. The executable workflows may
invoke external tools and mutate firmware or disks only when actually run.

| File | Bash functions | Review category |
| --- | ---: | --- |
| `.github/workflows/boot.yml` | 0 | CI configuration |
| `.github/workflows/font-qualification.yml` | 0 | CI configuration |
| `.github/workflows/icon-detection.yml` | 0 | CI configuration |
| `EfiLib/Makefile` | 0 | Build recipe |
| `Make.common` | 0 | Build recipe |
| `Makefile` | 0 | Build recipe |
| `README.md` | 0 | Engineering document |
| `boot-native-installer.run` | 26 | Installation/release script |
| `boot-sb-healthcheck` | 15 | Installation/release script |
| `boot-sbat-local.csv` | 0 | Configuration/data |
| `boot-sbat.csv` | 0 | Configuration/data |
| `boot.conf-sample` | 0 | Configuration/data |
<!-- BOOT_TEXT_INDEX_NEXT -->
