# Validation and Qualification

## Purpose

Validation establishes what the project actually knows about a change. It must not overstate support merely because source compiles or one automated job is green.

The validation standard is risk-proportional and follows the canonical Infiltrator Engineering Philosophy.

## Evidence classes

### Source and contract validation

This level checks structure, formatting where applicable, dependency identity, generated/vendor consistency, obvious ABI or symbol problems, and documentation consistency.

### Host build validation

This level proves that the selected firmware toolchain can produce the expected artifacts without unresolved host-runtime dependencies or build errors.

A successful host build proves compilation and linkage only. It does not prove firmware runtime behaviour.

### Firmware runtime validation

This level exercises the generated EFI application or driver in a representative firmware environment. Depending on the change, evidence may include virtual firmware, physical hardware, multiple firmware vendors, multiple architectures or Secure Boot configurations.

### Behaviour validation

This level verifies the product contract: discovery, ordering, configuration, input, presentation, loader handoff, fallback and useful failure states.

### Mutation and recovery validation

Any code capable of modifying NVRAM, boot order, ESP contents, partition metadata or other durable state requires dedicated tests for preconditions, interruption, verification and recovery. These paths must not be inferred safe from read-only discovery tests.

## Current automated qualification

The repository currently has focused automated coverage for the InfiltratorFS UEFI integration:

- building the read-only InfiltratorFS EFI driver with GNU-EFI on x86-64;
- rejecting unresolved host operating-system/runtime dependencies in the resulting shared object; and
- verifying that the committed vendored InfiltratorFS/Common/LZ4 tree matches the exact pinned source generator.

These checks are useful but deliberately not described as complete Boot qualification. The presence of older GNU-EFI, TianoCore and EDK2 build paths in the repository does not make all of them release-qualified automatically.

## Required change evidence

### Documentation-only changes

Must remain internally consistent, preserve legal/provenance truth, and not claim validation that has not occurred.

### Build-system changes

Must prove that affected supported build paths still produce expected artifacts. Changes to dependency resolution or vendoring must prove exact identity and reproducibility.

### Discovery/configuration/UI changes

Must exercise both successful and malformed/unsupported input and confirm deterministic ordering or fallback where relevant.

### Filesystem changes

Must include malformed-volume behaviour, bounded parsing/allocation and read-only contract validation. A successful mount of a healthy sample is insufficient.

### Trust-sensitive changes

Must validate accept/reject behaviour across the affected signature or trust states and prove that failure cannot silently become execution.

### Persistent mutation changes

Must include target validation, partial-failure behaviour, post-verification and a recovery exercise. These changes require the highest scrutiny.

## Architecture matrix

The current source contains paths for x86-64, IA32 and AArch64. A release may claim an architecture only to the extent that the release candidate has current evidence for that architecture. Historical build support is not current evidence.

Where only one architecture is automated, documentation and release notes must say so plainly rather than implying universal qualification.

## Artifact identity

Where practical, the artifact released should be the exact artifact that passed qualification. Rebuilding after qualification creates a new artifact and therefore a new evidence problem unless reproducibility has been established strongly enough to prove identity.

Vendored dependency identity must be represented by exact immutable commits and corresponding licence material. Mutable tags or branch names are insufficient as the only dependency identity for release qualification.

## CI behaviour

CI is a verifier, not a hidden source-authoring process.

Automated jobs may generate temporary comparison state in isolated directories, but they must not silently rewrite product source, create commits or push generated state to `main` merely because a workflow ran. Source changes should remain explicit repository changes subject to ordinary review and history.

## Release gate

Before release, answer with evidence:

- What product behaviour changed?
- Which architecture/toolchain combinations were built?
- Which firmware/runtime environments were exercised?
- Which failure states were tested?
- Did any trust or persistent-state authority change?
- Are vendored dependencies exact and reproducible?
- Are documentation and release claims limited to what the evidence proves?

A release is not qualified if those questions cannot be answered confidently for the risk introduced by the change.
