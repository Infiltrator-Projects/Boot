# Changelog

## Development preview 0.1.2 — 2026-10-17

- Publish the exact GNU-EFI x86-64 Boot application produced by CI as a versioned EFI development artifact.
- Publish the exact read-only InfiltratorFS EFI driver produced by the same build.
- Seal both EFI binaries, the tracked source archive and source-commit identity in one SHA-256 manifest and verify the complete remote release before and after publication.
- Keep firmware-runtime, Secure Boot, persistent-installation and recovery qualification explicitly separate; the preview performs no installation and does not modify ESP or NVRAM state.

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
