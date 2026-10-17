# Changelog

## 0.1.7 — 2026-10-17

- Complete Boot's memory-backed EFI driver path by passing no device path to firmware `LoadImage()` when a validated driver image is already supplied in memory.
- Remove the remaining firmware filesystem/device-path dependency from InfiltratorFS driver loading while leaving OS-loader handoff unchanged.
- Follow the UEFI memory-to-memory `LoadImage()` contract so real firmware cannot re-resolve the driver file path during this load operation.

## 0.1.6 — 2026-10-17

- Load EFI drivers from Boot-owned memory buffers after Boot has opened and validated them, avoiding firmware device-path re-resolution failures observed on real Dell UEFI hardware.
- Keep OS-loader handoff path-backed so Linux, Windows and macOS loader semantics are unchanged.
- Preserve the existing `drivers_x64` runtime layout and InfiltratorFS binary contract; this release changes Boot's driver-loading path rather than the filesystem parser.

## 0.1.5 — 2026-10-17

- Generate Boot's InfiltratorOS OS icon from the canonical pinned `InfiltratorOS/assets/infiltratoros.png` artwork and verify its exact Git blob identity before conversion.
- Provide `infiltratoros`, `infiltrator` and `InfiltratorBoot` icon hints from the same product-owned artwork so discovery can identify InfiltratorOS before its root filesystem is necessarily readable.
- Ship the runtime `icons` directory Boot expects, retaining functional and third-party OS glyphs while excluding historical boot-manager product identity icons.
- Configure the runtime explicitly with `icons_dir icons`.
- Publish a deterministic ready-to-copy x86-64 UEFI bundle rooted at `EFI/BOOT`, containing `BOOTX64.EFI`, `boot.conf`, MB firmware fonts, runtime icons and the InfiltratorFS driver in `drivers_x64`.
- Include icon provenance and the generated InfiltratorOS icon in the release checksum set.

## 0.1.4 — 2026-10-17

- Generate Boot-compatible firmware bitmap atlases from the canonical pinned MB Corpo archive used by InfiltratorOS.
- Verify the archive and all three source faces against their canonical SHA-256 identities before generation.
- Publish MB Corpo S Regular, MB Corpo S Bold and MB Corpo A Title Cond Regular firmware atlases with the Boot release.
- Configure Boot to use MB Corpo S Regular for its current single-font firmware UI.
- Include font provenance and all generated font assets in the release checksum manifest.

## 0.1.3 — 2026-10-17

- Rename Boot's runtime configuration contract from the inherited `refind.conf` name to `boot.conf`.
- Publish `boot.conf` as a verified release asset beside the EFI binaries.
- Keep removable-media qualification state off motherboard NVRAM by default while physical testing is in progress.

## 0.1.2 — 2026-10-17

- Publish the exact GNU-EFI x86-64 Boot application produced by CI as a versioned EFI artifact.
- Publish the exact read-only InfiltratorFS EFI driver produced by the same build.
- Seal both EFI binaries, the tracked source archive and source-commit identity in one SHA-256 manifest and verify the complete remote release before and after publication.
- Keep firmware-runtime, Secure Boot, persistent-installation and recovery qualification explicitly separate; the release performs no installation and does not modify ESP or NVRAM state.

## Source preview 0.1.0 — 2026-10-16

- Qualify the complete GNU-EFI x86-64 program alongside the read-only InfiltratorFS driver.
- Replace host-dependent default toolchain selection with explicit GNU-EFI defaults and preserve recursive make flags.
- Keep generic cleanup inside repository-owned outputs.
- Publish exact source as an immutable prerelease, with binary/runtime qualification explicitly separate.

All notable Boot project changes are recorded here.

This changelog tracks the Infiltrator project itself. Historical upstream change history imported with the initial source tree remains in `NEWS.txt` and is not duplicated here.

## Unreleased

### Changed

- Renamed the component to Boot across maintained documentation and integration metadata.
- Archived imported package definitions as source provenance so the current product cannot publish under the historical upstream package identity.
- Recorded InfiltratorOS's shared graphical product contract and emergency-only terminal policy.
- Reframed the repository around Boot as the product authority rather than its historical source basis.
- Added project-specific engineering, design, architecture, product-quality, safety, validation and provenance documentation under the canonical Infiltrator Engineering Philosophy.
- Added maintained build and contribution guidance.
- Defined inherited names and source structure as transitional implementation details rather than product identity.
- Reworked vendored InfiltratorFS maintenance so generation is explicit and CI verifies committed source instead of authoring and pushing source changes automatically.
- Tightened InfiltratorFS UEFI CI around explicit qualification semantics and immutable action identity.
