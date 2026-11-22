# Changelog

This file records changes supported by repository commits and existing release qualification. Version labels are assigned by GitHub Actions; a document-only commit does not itself establish a new functional boot capability.

## 2026-11-22 — Targeted documentation-contract corrections

- Replaced duplicated commentary on firmware boot-option menu construction with one explicit ownership, output and failure contract.
- Documented the unguarded offset arithmetic and cache-range assumptions in the EFI filesystem block adapter without suggesting those defects were fixed.
- Clarified that configuration file sizes narrow from UINT64 to UINTN, that short-read completeness is unproven, and that UTF-8-labelled input is not decoded into Unicode code points.
- Clarified that EFI driver discovery counts filenames rather than authenticated or successfully bound drivers.
- Documented fallback-loader comparison limits: unchecked file-information results, unbounded file-sized allocations and independently unchecked read lengths.
- Distinguished format recognition, firmware authentication, gzip size limits and successful operating-system boot at the EFI launch boundary.
- Added source-specific cross-boundary contracts to Architecture and executable regression acceptance criteria to Validation.
- All changes in this entry are comments or Markdown only; **no firmware code, scripts, packaged drivers, tests or executable behaviours were modified**. No new build, firmware or physical-system test result is claimed.
- This is a targeted correction, **not** a completed every-file postgraduate-level certification.

## 2026-11-22 — Code-comment and documentation-only review

- Added 115 source-level technical function contracts across 28 maintained C implementation files, covering firmware and menu entry lifetimes, file and variable ownership, C/EFI byte and block units, loader discovery, FSW stream mappings, parser assumptions, Unicode limitations and outstanding resource hazards.
- Relocated ten existing comment blocks from between return types and function names to proper declaration positions without rewriting the underlying declarations.
- Compared every touched C file with the pre-comment revision `bf1888a` after excluding comments and insignificant whitespace: no executable statements or expressions changed in this documentation task.
- Updated Architecture, Hardware, Validation and project documentation to distinguish inherited boot-manager design, currently observed automation, unresolved issues and unverified hardware compatibility. The earlier 22 November CI run passed firmware compilation, installer tests, host regressions and OVMF smoke testing but failed at prerelease publication; no success claim is made for that step.

## 2026-11-22 — Firmware variable and menu ownership follow-up

- Made the optional EfivarGetRaw output-size pointer work on both firmware and file-backed paths, zeroing outputs and rejecting invalid mandatory inputs.
- Freed temporary boot-option menu entries, owned title buffers and menu pointer arrays after selection; checked allocation and pointer-vector append results before adding choices.
- Preserved user-visible menu actions, firmware-image loading, filesystem modules and native installation paths. Firmware UI allocation fault-injection remains separate from source inspection.

## 2026-11-22 — EFI device-path commentary and string lifetime

- Added decoder-specific explanations of vendor, ACPI, network, iSCSI and unknown device-path node formatting, distinguishing display output from validated binary input.
- Made MyCatPrint appends transactional under allocation failure, bounded output growth to MAX_CHAR and tracked the length in CHAR16 units instead of bytes; removed redundant terminal reallocation.
- Preserved the existing EFI device-path protocol fallback and noted its unresolved lack of an external input byte bound. This change does not certify parsing of malformed firmware nodes.

## 2026-11-22 — Parser boundary and configuration-file lifetime corrections

- Bounded NTFS attribute-list name spans, MFT name offsets, directory index entries and compressed-page chunk-header reads before dereferencing on-disk input.
- Restricted ISO 9660 Rock Ridge SP probing to complete in-record structures.
- Released the firmware file handle and diagnostics on config allocation failure and released diagnostic text on success; returned output size is now zeroed at entry.
- These are targeted source corrections, not proof of NTFS/ISO fuzzing, firmware fault injection or filesystem Common V2 compliance.

## 2026-11-22 — Firmware boot-order and documentation correctness

- Eliminated a duplicate BootOrder-number case when the requested entry already headed the list; no-write behaviour remains for a genuinely unique first entry.
- Stale Boot#### reconciliation now treats EFI_NOT_FOUND separately from firmware read failures, stages changes before writing and does not overwrite BootOrder if every referenced entry is missing.
- Reported actual firmware write status in the boot-option management menu, distinguishing a completed Boot#### deletion from a subsequent BootOrder cleanup failure.
- Added source-extracted, host-only regression coverage. No changes were made to the native installer, generic package installation, filesystem ABI or EFI image-loading path.

## 2026-11-22 — Boot scanning and Btrfs memory-safety follow-up

- Bounded FindMem input lengths and corrected the last valid search offset, with compiled host regression cases.
- Freed individual Btrfs recovery-cache slots once each and corrected its logical free-byte statistic.
- Skipped alpha image composition on failed allocation.
- Strengthened source-level documentation of EFI directory ownership, firmware input semantics and Btrfs metadata handling.
- Clarified menu entry borrowing, graphics redraw lifetimes, RAID finite-field assumptions, and corrected outdated FSW large-offset and directory-retry explanations.
- Added native source-isolated regressions to CI without claiming physical firmware or Common V2 coverage.

## 2026-11-22 — Firmware input and display lifetime corrections

- Kept pointer protocol interfaces paired with their successfully opened EFI handles, including skipped opens; a failed protocol-array allocation no longer leads to a NULL protocol slot.
- Cleared freed cursor-image pointers to prevent repeated cleanup from using a stale image.
- Added allocation and width-overflow checks to the firmware text-mode blank-row buffer and skipped row erasure when allocation fails.
- Recorded the missing firmware-injection and repeated-cleanup regression evidence separately from the static fixes.

## 2026-11-22 — UTF-16 substitution and GUID parser correction

- Replaced the unsafe `ReplaceSubstring` pointer-to-pointer pool free with one bounded, transactional UTF-16 allocation, preserving its escaped-token semantics and the original string on failure.
- Repaired the impossible decimal-digit rejection condition in `IsGuid`, so nonhex characters no longer pass syntax validation.
- Recorded the need for firmware-hosted malformed-input and allocation-failure regression tests; C source inspection is not equivalent to executing those tests.

## 2026-11-22 — Post-audit accuracy and hosted test maintenance

- Updated installation and firmware-safety documentation to reflect replacement of BOOT's dedicated directory without prior ownership markers.
- Removed the obsolete standalone filesystem-driver install target, leaving the native `.run` and generic `.deb` as the supported deployment routes.
- Retargeted both hosted FSW test binaries to a selectable shipped backend (EXT2 by default), enabled large-file offsets and corrected POSIX test-adapter directory dnode and file-descriptor ownership handling.
- Recorded the compile-only boundary of hosted FSW tests; actual disk-image reads and UEFI hardware qualification remain separate validation tasks.

## 2026-11-22 — Confirmed defensive runtime fixes

- Preserved the full 64-bit FSW stream position for large-file reads.
- Rejected invalid EXT2 group geometry and unsupported block-size exponents before arithmetic.
- Added EFI volume/file handle allocation checks and restored a directory cursor after a failed EFI_FILE_INFO conversion.
- Returned a Btrfs registration allocation failure and guarded network volume allocation.
- Matched firmware handle classifications by their actual snapshot index; reported classification allocation failures instead of false success.
- Safely allocate and copy pointer-vector expansions before freeing the old buffer, preserving existing entries on allocation failure (the void append API still cannot propagate failure).
- Left installation behaviour, EXT4 recovery policy and Common V2 ABI unchanged.

## 2026-11-22 — Source commentary and documentation packaging

- Strengthened source-level explanations across BOOT menu/scan logic, filesystem metadata readers, decoder boundaries, UEFI boot protocols, signing/key-enrollment operations and native installer/test scripts.
- Replaced the obsolete package loop for deleted `.txt` documents with staging of maintained Markdown documents in the generic `.deb`.
- Added release assertions that fail if the engineering documents are missing from the staged package. No EFI firmware or native installation logic was modified in this change.

## 2026-11-22 — Documentation fidelity and comment-quality audit

- Corrected the Linux reinstall ownership-check description to reflect the actual delete-before-guard ordering.
- Recorded static FSW, EXT2, EXT4 and EFI directory-read limitations with their exact source locations and validation gaps.
- Defined concrete commentary, evidence and regression criteria without claiming that source inspection or prose changes repaired execution paths.
- Updated stale commentary status and icon-provenance references; did not modify firmware logic, installer behaviour or ABI.

## 2026-11-21 — Engineering documentation structure

- Introduced the central README and six technical documents, following the major documentation categories used by System Monitor.
- Documented the current EFI driver architecture separately from the unverified Common V2 module objective.
- No EFI, C source, filesystem driver, installer or CI behaviour changed by this documentation commit.

## 2026-11-21 — Repository cleanup

- [`4c60e44`](https://github.com/Infiltrator-Projects/Boot/commit/4c60e44b77ee8cf1edd7e00a19ddc02ee1a6b946): removed four old `.txt` documents.
- [`43393be`](https://github.com/Infiltrator-Projects/Boot/commit/43393bee7065210cccf97f20d1eaa2498ec6b9a6): deleted nine standalone licence documents from the current tree.
- [`dcf41e4`](https://github.com/Infiltrator-Projects/Boot/commit/dcf41e49625c6e17cca77c8205a2859850ddf1f4): removed obsolete `COPYING.txt` dependencies from the release scripts.
- [`555f60a`](https://github.com/Infiltrator-Projects/Boot/commit/555f60aab0d69867173415d44ec3f4092f25a577): stripped source and build-script comments before fresh technical documentation.
- [`f00fc27`](https://github.com/Infiltrator-Projects/Boot/commit/f00fc27c3b0b6365dd1365afbc02ee6cfa6d8c32): adjusted the installer-size check to account for stripped comments.
- [`6372845`](https://github.com/Infiltrator-Projects/Boot/commit/6372845992546f2419ea2114f09fa0eef1ef2e20): removed the optional Snowy theme resources.

## 2026-11-21 — Default boot-menu configuration

- [`0449d5d`](https://github.com/Infiltrator-Projects/Boot/commit/0449d5dda74a6711bdaa04be8d68cebd9b09247c): default configuration excludes GRUB and the EFI fallback from BOOT's display while preserving their separate firmware availability; prefers direct Linux kernel entries named `vmlinuz`.
- These configuration defaults do not restrict how many supported operating systems BOOT may discover.

## Historical scope

BOOT retains the established rEFInd-derived discovery and EFI loader mechanisms, together with six current EFI filesystem drivers. This record intentionally does not claim that Common V2 module loading or universal real-hardware qualification is complete.
