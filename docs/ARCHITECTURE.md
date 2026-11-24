# Architecture

## Purpose and execution boundary

BOOT is the UEFI boot manager owned by InfiltratorOS and intended to launch **other supported operating systems as well as InfiltratorOS**. It is a firmware-stage application, not an installed operating-system kernel, Linux VFS implementation or replacement for every other firmware boot entry. It must not assume a fixed list of installed systems.

The present executable is `boot/boot_x64.efi`, packaged and installed as `/EFI/InfiltratorOS/BOOT/BOOTX64.EFI`. The current main AMD64 build uses GNU-EFI. Some source files still contain conditional code and alternate firmware build infrastructure; their presence does not establish independently qualified releases for those architectures.

## From firmware entry to a selected loader

| Stage | State produced | Why the order matters |
| --- | --- | --- |
| `efi_main` calls `ScanVolumes`, then `ReadConfig` | A volume inventory and the configured scan order | Configuration can refer to a volume by name. Driver loading follows, and a positive `LoadDrivers` result triggers a fresh volume scan. |
| `ScanForBootloaders` visits the configured sources | Menu entries carry either a volume and file path or a firmware option number | Directory candidates are sorted before kernel folding. A folded sibling keeps its own path and options in the submenu. |
| `RunMainMenu` returns a selection and an exit reason | A pointer into the current menu inventory | Escape can rebuild that inventory. The old pointer belongs to the menu that supplied it. |
| `efi_main` dispatches on the entry tag | A child EFI image or a request for a firmware reset | A direct loader and a firmware boot option can have similar titles and still require different firmware calls. |

The branch point is visible in the two cases below. [Entry point](../boot/main.c) · [Menu records](../boot/global.h) · [Loader discovery](../boot/scan.c)

**Selecting a Linux EFI-stub file.** `SetLoaderDefaults` asks `GetMainLinuxOptions` for the primary command line. The first options-file entry supplies the initial arguments. If the kernel directory has no readable options file, the parser tries the volume's fstab and then the discovered root partition. `FindInitrd` searches beside the selected kernel and matches its extracted version. `StartLoader` eventually passes the path and resulting options to `StartEFIImage`. Firmware's loaded-image protocol receives the options length in bytes, including the final CHAR16 terminator. [Options parser](../boot/config.c) · [Kernel and initrd selection](../boot/linux.c) · [EFI handoff](../boot/launch_efi.c)

**Selecting a firmware entry.** Suppose the menu entry represents `Boot0007`. The entry contains numeric value 7. `RebootIntoLoader` writes that two-byte value to `BootNext` under the EFI global-variable GUID and requests a cold reset only after the write succeeds. The next firmware boot interprets `Boot0007`. Changing the persistent order uses a separate function, `SetBootDefault`. For example, promoting 7 in `[2, 7, 4, 7]` produces `[7, 2, 4]`, and later duplicates are removed even when the selected entry was already first. Actual firmware write errors are surfaced in the UI rather than being reported as success. [Firmware option records](../boot/install.c) · [One-time selection](../boot/launch_efi.c)

## Runtime component ownership

| Source | Responsibility |
| --- | --- |
| `boot/main.c` | BOOT lifecycle, initialisation, interface orchestration and main event loop |
| `boot/config.c`, `boot/config.h` | Configuration-file parsing and runtime selection settings |
| `boot/scan.c`, `boot/scan.h` | Volume/loader discovery, default icon choice, fallback handling and menu entries |
| `boot/menu.c`, `boot/screen.c`, `boot/pointer.c` | Firmware UI, selection, graphics/text display and user input |
| `boot/launch_efi.c` | EFI image validation/launch path and filesystem-driver connection during EFI handoff |
| `boot/linux.c` | Linux EFI-stub kernel-specific boot discovery and option handling |
| `boot/driver_support.c` | EFI driver binding and controller connection support |
| `boot/install.c` | BOOT installation-related firmware mechanisms within the UEFI program |
| `boot/lib.c`, `boot/mystrings.c` | UEFI-facing utilities and string/path handling |
| `libeg/`, `EfiLib/` | Graphics handling and EFI support routines |
| `mok/` | Existing Secure Boot-related code and interfaces |
| `filesystems/` | The six primary built EFI filesystem drivers and additional source/build targets |
| `boot-native-installer.run` | Single native build-and-install path for the versioned `.run` release |

The current codebase originated from the original upstream boot-manager implementation. It retains its discovery, menu and EFI filesystem mechanisms; no documentation should claim that those mechanisms were replaced by a new universal boot engine during the recent cleanup. Conversely, the operational history of an upstream boot manager is not transferable hardware certification for BOOT after changes in loader defaults, drivers, build profiles or installer behaviour. The meaningful compatibility claim is limited to the exact paths and environments actually exercised.

## Volume discovery and filesystem-driver contract

UEFI firmware exposes device handles and protocols. BOOT performs firmware-stage discovery and uses EFI filesystem drivers to make additional volumes readable. The GNU-EFI make target builds the EFI application and the EXT2, EXT4, ReiserFS, ISO 9660, HFS-family and Btrfs drivers as separately built `*_x64.efi` files. The native installer requires each of these drivers and installs them beneath BOOT's own `drivers_x64/` folder.

**Historical versus current evidence:** Historical FAT12/FAT16 provider records and the Kernel README describe a generic Common V2 ELF/UEFI bridge and OVMF kernel handoffs (Kernel identifies BOOT 0.1.39 for FAT16). Those records refer to earlier integration work; the current BOOT tree builds the separate EFI drivers and does not contain the previously referenced `tests/test_so_boot.py` qualification entry point. Do not transfer historical results to this revision. Restoring and qualifying the generic V2-only loader, then testing each provider, remains open.

This is the **current source filesystem integration**. It must not be conflated with the intended Infiltrator Common V2 filesystem interface. A Common V2 `.so`, a hosted filesystem driver, and a freestanding EFI firmware driver are different output types. Shared source or ABI compatibility cannot be inferred from identical filesystem names. The future V2 architecture requires explicit binary format, platform ABI, dependency, security, loading and read-path proofs; it is not declared completed here.

A loader is discoverable only if BOOT can reach its volume, identify a candidate and accept the candidate as a loader. A recognized filesystem signature is not a guarantee that its contents can be read. Similarly, discovery of a kernel file does not by itself prove its command line, initrd, EFI-stub compatibility or successful operating-system start.

## Boot selection and preservation of alternatives

The configured operating-system selection is a **presentation policy** on top of general discovery, not a set of hard-coded OS choices.

The currently maintained defaults in `boot.conf-sample` are:

```ini
timeout 20
use_nvram false
dont_scan_files +,grubx64.efi,EFI/BOOT/bootx64.efi
default_selection vmlinuz
```

The default file also contains disabled example menu stanzas. Their presence does not create active fixed menu entries. The `+` syntax for `dont_scan_files` retains existing exclusions while adding GRUB's usual filename and the EFI fallback path. Hiding those from the BOOT UI does **not** delete those executables or their independent firmware entries. On a given machine the firmware's one-time boot selector may still expose them.

`default_selection vmlinuz` prefers matching direct-kernel entries; it is not an assertion that every system has a working EFI-stub kernel. Other operating-system discoveries remain available. BOOT should be able to show every supported installation on systems with numerous volumes or operating systems. Display filtering is separate from firmware-level recoverability.

`use_nvram false` is a BOOT runtime configuration setting; it is **not** a prohibition on the native installer using UEFI NVRAM to register and prioritise the BOOT firmware entry.

## Installation boundaries

The published native `.run` is a single, complete installation script. It elevates when necessary, verifies a source revision pinned by the release, builds BOOT and the six drivers, and invokes its own existing install flow. It has no permitted alternate ESP destination and no build-only execution mode.

The destination is strictly `/EFI/InfiltratorOS/BOOT`, with executable `BOOTX64.EFI`. Both the native `.run` and generic `.deb` replace the complete contents of BOOT's reserved directory, including previous configuration. No prior file-ownership check or checksum marker is required. The fixed destination and symbolic-link protections remain; Windows, GRUB and EFI fallback paths are left untouched.

The installer calls its firmware-entry management routines to establish BOOT as the preferred firmware choice. The tests exercise detection of duplicate/stale entries, preservation of non-BOOT entries and repeated installation ordering; those are distinct from the BOOT runtime menu's choices. Failure to identify firmware state correctly must not be papered over with a second BOOT entry.

The generic AMD64 `.deb` installs its precompiled EFI binary, six drivers and resources at package configuration via `packaging/install-generic.sh`, registers BOOT in UEFI NVRAM and makes it first in BootOrder. It needs a mounted VFAT ESP and a UEFI-capable host; unlike `.run`, it does not build from source.

## Build and release ownership

`Makefile` calls the GNU-EFI build for `libeg/`, `mok/`, `gzip/`, `EfiLib/`, `boot/` and `filesystems/`. `packaging/build-release.sh` pins the exact source commit in the downloadable `.run` and creates an installing generic `.deb` plus checksums. `packaging/test-release.sh` verifies artifact metadata, expected driver contents and native-install structure.

`.github/workflows/boot.yml` owns current AMD64 verification and automated stable release publication; this release classification is separate from physical-hardware qualification. The source tree also includes older or alternative make targets; only the explicit native release path is authoritative for the shipped `.run`.

## Font-file provenance, verification and firmware rasterisation

The published font inventory contains 23 MB/Daimler TrueType files with
a `fonts/SHA256SUMS` manifest. The native and generic installation
paths check that manifest and the 23-file inventory before installing.
This is a packaging integrity condition, not a runtime cryptographic
attestation inside the UEFI text renderer.

`libeg/ttf.c` contains the real outline renderer. Each registered font
is loaded from BOOT's own EFI directory and passed through structural
TrueType table checks before `stb_truetype` initialisation. Three
provisional role mappings select among the full inventory; they are not
a decision to discard the other 20 fonts. A rasterisation probe checks
that the role-selected faces produce painted pixels. A successful
offscreen probe is not equivalent to correct real-world screen output,
subpixel appearance, glyph coverage in every locale or successful
loading on every firmware. The hosted `tests/validate-fonts.c` fixture
also uses `stb_truetype` directly and is not a substitute for exercising
UEFI allocation and graphics code.

The firmware-local replacement for selected mathematics and memory
operations has its own numeric and failure assumptions. In particular,
the renderer caps input file size, glyph bitmap dimensions and cached
ASCII glyphs. Its dependence on the upstream TrueType parser makes
defensive table validation and real firmware testing separate obligations.
Neither a fallback font nor a drawn replacement glyph establishes which
registered face produced the image. The QEMU/OVMF font smoke test checks
a narrower rendered-glyph contract, not complete font fidelity.

## Graphical presentation and product identity

**Art direction: ELEGANCE.** BOOT's appearance must be artistically refined, not merely sparse or technically tidy. Its composition, visual balance, typography, spacing, shapes, icon proportions, image integration and detail should be judged as a complete, deliberately crafted design. See [Engineering Philosophy](PHILOSOPHY.md#elegance--the-design-approach); aesthetic elegance never means deleting useful source comments or reducing engineering documentation.

The repository ships one official InfiltratorOS default look, but `boot.conf` remains the user-controlled theme mechanism: configured PNG/JPEG assets may be replaced or disabled without altering the EFI launch machinery. The ability to customise does not require shipping alternative theme collections.

The menu code can load supplied icons, banners, fonts, selection images and other resources. Those are lower-level capabilities of the present firmware renderer, not approval for a user-installable theme ecosystem. BOOT is intended to carry one official InfiltratorOS visual design. The optional Snowy assets have been removed from the repository; the basic icon/graphics infrastructure remains.

Appearance changes must not alter scan ordering, delete other loaders, disable filesystem drivers or change the native installer. A missing image cannot be allowed to invalidate an otherwise sound boot path.

## Integrity and trust boundaries

There are distinct integrity boundaries: the `.run` pins its downloaded source to the release commit; both installers restrict ESP writes to BOOT's fixed directory; EFI image and driver authentication depends on the active firmware trust configuration. Installation-path checks must not be confused with Secure Boot image authentication.

Input surfaces include ESP directory entries, filesystem metadata, configuration text, EFI load options, firmware NVRAM state and optional hardware-dependent paths. EFI device-path text formatting now preserves its UTF-16 append buffer on allocation failure, but the fallback formatter still accepts an unbounded device-path pointer and must never be presented as a binary validator. Malformed or missing data must not be represented as successful OS discovery, authenticated execution or a verified firmware update.

## Secure Boot and signing side-effect boundaries

BOOT's native installer distinguishes signing keys from firmware/shim authentication of launched EFI images. `GenerateKeys` and `SignOneBinary` handle signing material; `InstallSBKey` may request MOK enrollment requiring later firmware confirmation. The generic `.deb` installs its prebuilt image without claiming that Secure Boot will accept it.

The separate `boot-sb-healthcheck` script reads trust-database and certificate metadata but also contains interactive paths that replace keys or initiate further administrative actions. It is not a purely observational scanner, and certificate expiry alone is not proof of a Secure Boot failure.

## Cross-boundary contracts requiring explicit qualification

### FSW block I/O and cache geometry

`filesystems/fsw_efi.c:fsw_efi_read_block` receives a logical physical-block index, converts it into a byte offset using `phys_blocksize`, and reads through firmware Disk I/O. The adapter holds two 128 KiB read-ahead windows; a cache hit is meant to be restricted to the same mounted volume and to a window containing the full requested block. When a wide read or cache allocation fails, it falls back to a one-block read. `LastIOStatus` records the resulting firmware status.

This path does **not** currently establish that multiplication of the block index and block size fits the offset width. It also performs endpoint additions in cache-hit checks without explicit overflow proof, and assumes that the caller supplies a buffer large enough for `phys_blocksize`. These requirements are preconditions in practice, not validated properties. The cache must not be treated as an independently trustworthy source of media identity after controller or media changes without corresponding invalidation.

### Fallback-loader equivalence

`boot/scan.c:DuplicatesFallback` opens the candidate and architecture fallback from the same volume. Equal reported file sizes allow it to allocate two buffers, read the contents and compare them. As written, the routine does not impose a file-size cap, does not check a possible NULL from `LibFileInfo`, and does not independently confirm both returned read lengths before `CompareMem`. Its boolean result is a menu-filtering heuristic, **not** a secure equivalence or authenticity assertion. `FALSE` can mean that either file was unavailable or unreadable, an allocation failed, or the bytes differed.

### EFI child-image execution

`boot/launch_efi.c:StartEFIImage` first classifies a candidate loader, then delegates loading to UEFI `LoadImage`. The classifier is not an authenticity check. For gzip input, BOOT rejects compressed files that are empty or larger than `512 MiB / 10`, allocates at most ten times the compressed size, and passes the decoder's actual reported output byte count to `LoadImage` only after successful decompression. A `LoadedImage` options buffer is held across `StartImage`; the size is counted in bytes and includes the CHAR16 terminator. A child image that loads or returns does not establish successful operating-system boot. Driver images deliberately remain resident when needed for installed protocols.

### Configuration-file length and encoding semantics

`boot/config.c:ReadFile` obtains an advertised `UINT64` file length, narrows it to `UINTN` for allocation and uses the eventual read byte count as `BOOT_FILE.BufferSize`. The separately returned size is assigned before the read. There is no explicit pre-allocation check that the advertised length is representable in `UINTN`; a successful read status alone need not imply that the advertised file length was fully consumed. BOM detection distinguishes UTF-16LE and a UTF-8-labelled path, but `ReadLine` currently widens individual bytes rather than decoding UTF-8 sequences. Consequently, non-ASCII UTF-8 text does not have a demonstrated Unicode-correct parse contract.

### Driver file discovery versus driver binding

`boot/driver_support.c:ScanDriverDir` increments its count upon finding a candidate EFI file, before inspecting the result of `StartEFIImage`. `LoadDrivers` uses a positive count to trigger reconnection and a volume rescan. That Boolean reports discovered candidate files, not confirmed driver installation, authentication, protocol binding or filesystem readability. Any operational claim about newly readable volumes requires inspection after the rescan and an actual read-path test.

### Evidence distinction

These descriptions are source-inspection findings. They do not imply that fuzzing, hostile filesystem images, fault-injected EFI allocation, firmware authentication matrices, or physical hardware regression runs have occurred. The related unexecuted regression requirements are tracked in [Validation](VALIDATION.md). Documentation changes do not repair the implementation.

## Source contract: image classification versus authenticated execution

`boot/launch_efi.c:IsValidLoader` is a format classifier, not a complete
PE validator or a signature verifier. Its current native-image probe requests
4096 bytes, reads the DOS PE-offset field before confirming that the requested
number of bytes was returned, and compares an architecture signature only
when the probe has the expected full size. It recognises a gzip magic header
separately when that configuration is enabled. Firmware `LoadImage`, together
with active security policy, performs a later decision about accepting the
image; menu discovery alone does not establish that the image will execute.

The function presently allocates its probe buffer before rejecting missing
`RootDir` or `FileName`; the early returns on missing arguments and failed
`Open` do not release that allocation. This is a **known implementation
defect**, not a memory-safety property. A short successful `Read` also does
not establish that bytes used for its PE-offset field were initialised.
These risks require code changes and regression tests; source commentary
must not claim they have been repaired.

### Classification, de-duplication and driver discovery are distinct predicates

`IsValidLoader` answers a format-classification question, not whether the image
is trustworthy or will execute. Its NULL-argument branch presently returns
`LOADER_TYPE_EFI` without examining a file, whereas a failed EFI `Open` returns
`LOADER_TYPE_INVALID`. This asymmetry means callers cannot treat its positive
result as proof of a successful file read. The unchecked early allocations and
short-header access are documented above as defects, not intended contracts.

`boot/scan.c:DuplicatesFallback` answers a different question: whether a
candidate resembles the fallback file sufficiently to suppress a duplicated
menu entry. It opens the two files separately, allocates based on reported
`EFI_FILE_INFO.FileSize`, and compares data without independently proving that
both reads returned their advertised lengths. Its `FALSE` also covers I/O and
allocation failure. It does not prove binary identity, on-disk immutability or
EFI signature validity. Separately, `ScanDriverDir` counts *candidate files*,
not confirmed EFI-driver binding. Neither scan result can be promoted into an
OS-boot or filesystem-readiness guarantee.

## Source contract: firmware BootOrder enumeration

`boot/install.c:FindBootOrderEntries` receives `BootOrder` as a byte-sized EFI
variable, rejects odd byte counts, and iterates its 16-bit option numbers.
For each readable `Boot####` record it bounds the UTF-16 label terminator
and the declared device-path span before separately allocating owned copies.
Missing and malformed records are skipped. Allocation failure terminates
enumeration and may return an already-built *prefix* of entries. Thus a
returned list is a presentation inventory, **not proof that every firmware
option was read**. A `NULL` result does not distinguish an empty/invalid
inventory from some allocation or variable-read failures.

`DeleteInvalidBootEntries` has a different failure contract: it only
discards `EFI_NOT_FOUND` records, aborts on other read errors and avoids a
`BootOrder` rewrite when no remaining option could be read. The deletion of
a `Boot####` option and subsequent `BootOrder` cleanup are separate
operations. A successful first mutation followed by cleanup failure is a
partial firmware change, and documentation or UI messages must reflect it.

## Source contract: driver binding lifetime

`filesystems/fsw_efi.c:fsw_efi_DriverBinding_Start` borrows `BlockIo`
through `EFI_OPEN_PROTOCOL_GET_PROTOCOL`, opens `DiskIo` with
`EFI_OPEN_PROTOCOL_BY_DRIVER`, allocates an owning volume record, mounts the
FSW backend and publishes a Simple File System interface only after mount
success. On failure the volume is unmounted/freed and the claimed Disk I/O
protocol is closed. On success the embedded protocol storage remains alive
until `fsw_efi_DriverBinding_Stop` can uninstall that interface. If firmware
refuses uninstall, Stop returns without freeing the volume. A firmware
protocol install is an interface registration, not proof that reads from a
malformed or unsupported volume are safe.

## Interface contracts that require careful interpretation

### Firmware boot-option parsing

`FindBootOrderEntries` consumes `BootOrder` as a sequence of 16-bit option numbers, obtains matching `Boot####` variable records, and constructs a caller-owned linked list. A record is not a valid candidate merely because the variable read succeeded: its fixed header, terminated UTF-16 description, bounded file-path span and terminal device-path node must all be consistent. The parser does not authenticate the executable at that path, establish that it exists, or certify that firmware can launch it. [Implementation](../boot/install.c)

The parser is deliberately **not an all-or-nothing enumeration**. Malformed or missing options are skipped; an allocation failure can return a valid prefix. Consequently, an empty or shortened result is ambiguous: it must not be used as proof that the firmware has no more options. By contrast, `DeleteInvalidBootEntries` reads every referenced option before publishing a replacement `BootOrder`, and treats read errors other than `EFI_NOT_FOUND` as barriers to mutation. Its cleanup is not transactional with the preceding deletion of a `Boot####` variable: deleting the option may succeed while rewriting `BootOrder` fails.

### Controller rebinding and filesystem visibility

`ConnectFilesystemDriver` enumerates Disk I/O handles, ignores non-logical partitions and volumes already exposing Simple File System, then inspects driver-open records. A successful disconnect permits a new `ConnectController` attempt. The routine returns `VOID` and does not propagate the outcome of these firmware calls. Controller reconnection can change the set of accessible volumes, so discovery must be repeated. [Implementation](../boot/driver_support.c)

`LoadDrivers` returns a Boolean derived from matching driver filenames, **not** a count of successfully launched or bound drivers. A positive result authorises a rescan but is not proof of filesystem readability. The installed EFI driver protocol, the filesystem's mount operation and an actual file read are independent evidence stages. This is the current firmware-driver design; it is not the proposed Common V2 `.so` interface.

### Filesystem-driver resource ownership

`fsw_efi_DriverBinding_Start` borrows Block I/O with `GET_PROTOCOL`, acquires Disk I/O with `BY_DRIVER`, allocates a volume record, mounts the filesystem-neutral FSW backend and publishes Simple File System only after mounting succeeds. The installed protocol is embedded in the volume allocation, so the allocation cannot be released while the protocol remains published. Failure cleanup unmounts, frees and closes the driver's Disk I/O acquisition. [Implementation](../filesystems/fsw_efi.c)

`fsw_efi_DriverBinding_Stop` must successfully uninstall Simple File System before freeing its containing volume; otherwise firmware could retain a pointer into freed memory. After uninstall it unmounts, releases the volume, closes Disk I/O and clears the FSW cache. These paths require fault-injected protocol-install/uninstall tests and real metadata-read tests before their failure behaviour can be treated as qualified across firmwares.

## Source-visible hazards outside the current regression envelope

The comments on the following routines now state observed limitations rather than imply safety that the implementation does not establish:

- [`GenerateWaitList`](../boot/menu.c) allocates its global EFI event array without checking the allocation before indexing it. A later invocation may replace the global pointer without releasing the earlier array. Its caller must not interpret the VOID return as evidence of allocation success.
- [`IsSymbolicLink`](../boot/scan.c) compares EFI directory metadata with a separately opened file's length. It does not prove symbolic-link identity, can classify read failures as mismatches, and does not currently close a successfully opened file handle.
- [`GetFirstOptionsFromFile`](../boot/config.c) copies the first Linux command-line entry but frees only the `BOOT_FILE` wrapper; the options-file payload's lifetime must be established independently.
- [`EfivarGetRaw`](../boot/lib.c) may use BOOT's file-backed variable directory for the BOOT vendor GUID; a failed file read after opening that directory does not trigger a firmware-variable retry. Firmware allocation capacity is derived from a pointer-size multiplication rather than a literal maximum byte count.

These are defects or behaviour limits documented from source inspection, **not corrections to execution**. The validation plan must specify corresponding failed-allocation, interrupted-read and repeated-call fixtures before the affected contracts can be declared qualified.

### Linux loader-option and cache ownership boundaries

[`StoreLoaderName`](../boot/main.c) writes a UTF-16 title for later menu selection but has a VOID result and discards the EFI variable-write status; persistence cannot be asserted from its return. [`AddInitrdToOptions`](../boot/linux.c) returns a separately allocated option string while borrowing its inputs, and presently has an unguarded route to string-search operations when options are NULL but an initrd is supplied.

[`fsw_block_get`](../filesystems/fsw_core.c) gives the caller a pinned, borrowed cache buffer. Its reference count must be released before the containing volume or cache is destroyed. Cache growth uses a doubling strategy without locally checking all integer-size arithmetic; exhaustion and extreme size cases need independently verified behaviour.

## Unverified architectural objectives

The desired Common V2-only filesystem module design, universal operating-system compatibility, production-grade security qualification and complete custom InfiltratorOS appearance require explicit future evidence. This document records the implemented BOOT revision and does not silently declare those objectives done. See [Hardware](HARDWARE.md) and [Validation](VALIDATION.md) for observed limits.


## State, resource ownership and failure contracts

The following table records code-level relationships rather than a promise of complete correctness. A source file may implement additional paths not represented here.

| Transition | Inputs and mutable state | Ownership / failure consequence | Primary implementation |
| --- | --- | --- | --- |
| Firmware entry → volume scan | EFI image handle, system table, controller protocol database | Volume records depend on firmware handles and successful protocol acquisition; an inaccessible filesystem is not an empty one | `boot/main.c`, `boot/lib.c` |
| Network discovery → optional iPXE child | iPXE discovery EFI file and loader on BOOT's own volume | Unlike disk scanning, this can call firmware LoadImage/StartImage while constructing the menu; exit data and cloned volume ownership need separate scrutiny | `boot/scan.c` |
| Configuration → menu inventory | Config bytes, volume identifiers, scan/exclusion flags | Parser state determines visibility. Hiding a candidate does not delete an EFI file or NVRAM record | `boot/config.c`, `boot/scan.c` |
| Menu selection → action | Selected menu record, entry tag, current inventory | Menu entries and their submenus belong to the inventory that produced them; a rescan invalidates previous selection pointers | `boot/menu.c`, `boot/main.c`, `boot/global.h` |
| EFI launch | Candidate path, volume handle, optional UTF-16 load options | EFI image classification precedes authentication/firmware launch. Option buffers must survive the firmware call and statuses must propagate | `boot/launch_efi.c` |
| Firmware Boot#### selection | Binary NVRAM option and device-path metadata | `BootNext` is a one-shot selection. Persistent `BootOrder` promotion deduplicates the selected number; stale-record cleanup drops numbers only for EFI_NOT_FOUND. UEFI write errors are displayed to the user | `boot/install.c`, `boot/launch_efi.c` |
| Filesystem mount/read | Block and Disk I/O protocols, on-disk metadata, FSW dnodes/streams | Per-volume caches and open streams have separate lifetimes; a recognised magic number is not proof of a valid directory or file | `filesystems/fsw_efi.c`, `filesystems/fsw_core.c` |
| Native or generic reinstall | Mounted ESP, BOOT directory and BootOrder records | The reserved BOOT directory is intentionally replaced; unrelated EFI bootloaders remain untouched | `boot-native-installer.run`, `packaging/install-generic.sh` |

### Firmware image and directory ownership

The directory iterator borrows its caller's parent directory and owns only a successfully opened relative child. Graphics retains a banner independently of the replaceable background snapshot; compositing owns its temporary image. A failed image allocation must not be dereferenced, but that source guard is not equivalent to an EFI fault-injection test.

### Filesystem read and error boundaries

Btrfs cleanup frees every recovery-cache buffer through its own array slot rather than repeatedly freeing slot zero. Its EFI free-space statistic is now `max(total_bytes - bytes_used, 0)`, a logical-space estimate rather than a promise that profiled or degraded devices are writable. Native source-isolated tests exercise both rules, not full firmware execution.

The FSW core expresses a file as a stream over logical extents. A physical extent translates through block caching, a buffered extent provides decoder- or backend-owned bytes, and a sparse extent supplies zero bytes. The backend's extent units and the host's physical block units must remain distinct. Open streams retain independent positions even where they share a volume cache. See `filesystems/design.dox` for a worked block-boundary example.

This is a reader design, **not Common V2 conformance**. The shipped driver-binding layer publishes UEFI Simple File System protocols. It does not authenticate, dynamically link or execute Common V2 `.so` modules.

### Corrected runtime defects and still-open boundaries

At commit `a5c680e`, the following source-level failures were corrected: the FSW large-file reader no longer narrows the 64-bit position; EXT2 rejects zero inode-group counts and unsupported block-size exponents before arithmetic; FSW EFI volume and file-handle allocations are checked; EFI directory information errors restore the prior cursor; Btrfs master registration and network volume cloning handle pool exhaustion; list expansion preserves the original vector on allocation failure; and GNU-EFI controller classification uses the selected handle's index in its own snapshot. These are **implemented corrections, not claims of a completed fault-injection or real-hardware validation campaign**.

The list API `AddListElement` remains `VOID`: it now avoids dereferencing a failed allocation but cannot report a dropped append to its caller. Firmware boot-menu construction checks whether its entry count actually increased before retaining an entry and frees its allocated menu records after selection. The output-size argument to `EfivarGetRaw` now follows its optional declaration. Those targeted corrections are not a general guarantee that every caller handles a dropped append.

The EXT4 reader currently admits the RECOVER feature bit and does not replay a journal. This is the existing read-only behaviour, not proof that an observed boot has failed; consistency behaviour on recovery-required volumes remains unqualified. Targeted source guards now constrain NTFS attribute-name offsets, index-entry name lengths and ISO SUSP SP marker reads; the existing NTFS compressed/attribute decoder and ISO continuation-area reader still require malformed-image regression evidence. These narrowly scoped changes do **not** establish complete parser safety or boot compatibility.

The obsolete standalone `filesystems/Makefile` installation target has been removed; only the installing native `.run` and generic `.deb` are supported deployment paths. The POSIX filesystem exerciser now defaults to its shipped EXT2 backend instead of referencing the absent XFS source. Hosted parser compilation does not qualify EFI driver binding, Secure Boot or actual volume reads.

## Source-level boundary and ownership cross-reference

The following map identifies non-obvious contracts recorded in the maintained source. These are *descriptions of the implementation*, not assertions that all branches were fault-injected.

| Boundary | Observed contract or implementation limitation | Source |
| --- | --- | --- |
| Entry and rescanning | `efi_main` establishes firmware services before discovery; `RescanAll` invalidates selections tied to the previous inventory. `SecureBootUninstall` requests a reset if policy-hook removal fails | `boot/main.c`, `boot/scan.c` |
| Loader and tool discovery | `ScanEfiFiles` checks each available volume, and `IsValidTool` applies visibility rules; ELF/PE detection and menu publication do not establish image authentication or successful startup | `boot/scan.c`, `boot/launch_efi.c` |
| Menu ownership | Entries and selected pointers belong to the current scan. Editing load options replaces an allocation only on editor success; hiding a tag persists BOOT-private display preferences | `boot/menu.c`, `boot/line_edit.c` |
| Firmware variables | `EfivarGetRaw` returns an owned buffer; private BOOT variables can use file storage, while EFI-global `BootOrder` is firmware state. `BootNext` is one-shot, unlike persistent order changes | `boot/lib.c`, `boot/install.c`, `boot/launch_efi.c` |
| Firmware device paths and BDS | Text printers operate on firmware-supplied node formats without an independently bounded whole-path buffer. Controller reconnection changes firmware handle topology and is not a filesystem read test | `EfiLib/DevicePath.c`, `EfiLib/BdsConnect.c`, `boot/driver_support.c` |
| Shared filesystem reader | An FSW dnode carries a volume/parent reference; streams own independent byte cursors, while extent mapping and cached physical blocks have distinct units and lifetimes | `filesystems/fsw_core.c`, `filesystems/fsw_efi.c` |
| EXT2 and EXT4 | An inode is fetched from a block-aligned inode table and copied out of the borrowed cache block. The EXT4 reader's current low-word file-size handling and non-replaying journal policy remain explicit limits | `filesystems/fsw_ext2.c`, `filesystems/fsw_ext4.c` |
| NTFS and HFS | NTFS `$UpCase` comparisons are not full Unicode normalisation, and its zero free-byte result is unimplemented accounting, not evidence of a full disk. HFS used-space figures rely on volume metadata | `filesystems/fsw_ntfs.c`, `filesystems/fsw_hfs.c` |
| ISO 9660 and ReiserFS | ISO Rock Ridge name continuations and ReiserFS tree item offsets originate on disk. Directory-entry names borrow record bytes until node construction copies them; untested malformed continuation cases remain | `filesystems/fsw_iso9660.c`, `filesystems/fsw_reiserfs.c` |
| Btrfs | Tree and inode numbers jointly identify nodes across subvolumes; RAID/parity paths use their own stripe and recovery-cache ownership. Metadata access is not a promise of support for every device topology | `filesystems/fsw_btrfs.c` |
| Graphics and font handling | Image planes use EFI pool ownership, and UI cursor snapshots must not survive restoration. `egDisplayMessage` currently does not free its temporary rendered image; font getters assume successful/preceding preparation | `libeg/image.c`, `libeg/screen.c`, `libeg/ttf.c` |

This review deliberately left those runtime and resource-management limitations unchanged. A comment-only change cannot be presented as a repair to an unverified algorithm or a substitute for firmware and malformed-image regressions.

Evidence and remaining regressions are listed in [Validation](VALIDATION.md).

## Commentary and implementation evidence boundaries

The maintained interface headers describe the ownership and side effects that a caller must understand before invoking a firmware routine. [Loader discovery](../boot/scan.h) distinguishes menu population from execution, but the network discovery path may invoke an EFI child during scanning; treating every scan as passive would be incorrect. [Volume and directory interfaces](../boot/lib.h) distinguish a recognised volume from an open firmware filesystem, document the directory iterator's short-lived entry buffers, and call out the existing `FindVolumeAndFilename` output-ownership trap. These contracts document present code rather than silently changing it.

[Boot-variable compatibility declarations](../EfiLib/GenericBdsLib.h) distinguish parsing, controller connection and persistent firmware-variable mutation. [Device-path formatting](../EfiLib/DevicePath.c) reports diagnostic strings but has no independently bounded source length in its fallback walker. [The additional NTFS reader](../filesystems/fsw_ntfs.c) documents low-level on-disk offsets, MFT and index traversal and current validation limitations; it is not among the six default installed drivers.

Comments are not proof of correctness. The implementation, tests, and precise tested revision are separate forms of evidence. A documented unchecked allocation or boundary condition is an unresolved engineering limitation until production behaviour and a targeted regression establish its disposition.
