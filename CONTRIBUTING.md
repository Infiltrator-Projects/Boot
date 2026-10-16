# Contributing

Boot is governed by the canonical Infiltrator Engineering Philosophy and the repository-specific rules in `PHILOSOPHY.md`.

Changes are judged by engineering quality, not by novelty, code volume or how completely they resemble the source tree from which the project originally began.

## Before changing code

Read, as applicable:

- `PHILOSOPHY.md`;
- `docs/DESIGN.md`;
- `docs/ARCHITECTURE.md`;
- `docs/PRODUCT-QUALITY.md`;
- `docs/SAFETY.md`;
- `docs/VALIDATION.md`; and
- `docs/PROVENANCE.md` when touching inherited or licensed material.

A meaningful change should identify the product problem, the owning concern, the relevant failure modes, compatibility implications and the evidence required to validate it.

## Ownership first

Do not solve a local symptom by creating a second authority.

If boot ordering belongs to boot-flow policy, do not duplicate it in presentation code. If configuration semantics belong to the configuration layer, do not reinterpret the same setting in several features. If a firmware quirk belongs at the firmware boundary, do not spread checks for it across the product.

When an apparently small feature needs unrelated modules to know about it, inspect the boundary before adding another special case.

## Inherited code

Inherited code is neither protected from change nor targeted for replacement merely because it is inherited.

Retain it when it remains the strongest justified implementation. Refactor it when ownership is unclear. Replace it when a stronger product-owned design is justified. Delete it when its supported contract has ended.

Historical names should normally be removed as part of a real boundary improvement rather than through broad cosmetic renaming. Preserve required attribution and licence notices.

## Safety-sensitive changes

Changes that affect any of the following require explicit safety reasoning and validation:

- firmware variables or boot order;
- ESP writes or installation behaviour;
- partition-table or disk-metadata mutation;
- filesystem write authority;
- Secure Boot, signatures, shim or MOK behaviour;
- loader handoff;
- parsing or decompression of untrusted disk/configuration data; or
- resource ownership at firmware boundaries.

Failure and recovery behaviour must be designed alongside success behaviour.

## Dependencies and vendored source

Dependencies must have an explicit purpose and contract. Exact immutable source identities are preferred for vendored or release-critical code.

The InfiltratorFS UEFI vendor tree has one generator: `scripts/vendor-infiltratorfs.sh`.

To verify the committed tree without changing it:

```sh
./scripts/vendor-infiltratorfs.sh --check
```

To intentionally regenerate the committed vendor tree from the pinned identities:

```sh
./scripts/vendor-infiltratorfs.sh
```

Inspect and commit the resulting diff explicitly. CI verifies the tree; CI does not author and push vendor updates automatically.

## Comments

Comments should preserve engineering reasoning that would otherwise be lost: invariants, firmware assumptions, ownership/lifetime rules, ABI/protocol constraints, trust boundaries, units/ranges, crash or interruption behaviour, compatibility reasoning and why a tempting alternative is wrong.

Do not add comments that merely translate C syntax into English.

## Documentation

If a change alters product behaviour, ownership, safety boundaries, supported build evidence or dependency provenance, update the authoritative documentation in the same change.

Historical imported documentation should not be rewritten to pretend it originated as Infiltrator work. Current Infiltrator documentation should not defer product authority to historical documents.

## Validation

Run validation proportionate to risk. A clean compile is not enough for changes to boot flow, firmware runtime behaviour, trust, filesystems or persistent state.

Report what was actually tested. Do not describe an architecture, firmware or toolchain as qualified merely because old build support still exists in the tree.

## Completion standard

A change is ready only when it leaves the system at least as understandable, trustworthy and coherent as before. If it adds permanent complexity, that complexity must be irreducible and justified by the problem being solved.
