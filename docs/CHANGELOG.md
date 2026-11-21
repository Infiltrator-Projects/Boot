# Changelog

This file records changes supported by repository commits and existing release qualification. Version labels are assigned by GitHub Actions; a document-only commit does not itself establish a new functional boot capability.

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
