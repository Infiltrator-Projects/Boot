# Changelog

All notable InfiltratorOS Boot Manager project changes are recorded here.

This changelog tracks the Infiltrator project itself. Historical upstream change history imported with the initial source tree remains in `NEWS.txt` and is not duplicated here.

## Unreleased

### Changed

- Reframed the repository around InfiltratorOS Boot Manager as the product authority rather than its historical source basis.
- Added project-specific engineering, design, architecture, product-quality, safety, validation and provenance documentation under the canonical Infiltrator Engineering Philosophy.
- Added maintained build and contribution guidance.
- Defined inherited names and source structure as transitional implementation details rather than product identity.
- Reworked vendored InfiltratorFS maintenance so generation is explicit and CI verifies committed source instead of authoring and pushing source changes automatically.
- Tightened InfiltratorFS UEFI CI around explicit qualification semantics and immutable action identity.
