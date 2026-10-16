# Boot Engineering Philosophy

This repository follows the canonical [Infiltrator Engineering Philosophy](https://github.com/Infiltrator-Projects/Infiltrator-Libraries/blob/main/docs/PHILOSOPHY.md). This document specialises that philosophy for a firmware-stage boot manager. It does not replace or weaken the canonical standard.

## Product authority

Boot owns its product behaviour.

Boot belongs to the single InfiltratorOS product and shares its visual and
interaction language. The ordinary boot and installation experience is
graphical. A hidden terminal is reserved for absolute emergency recovery.
Public application, package and publication identities use the current
component name; historical names are retained only where source provenance or
an internal implementation detail still requires them.

The repository began from an existing EFI implementation, but inherited code has no special architectural authority because of its age, origin or existing shape. It may be retained when it remains the strongest justified implementation, changed when a stronger boundary or behaviour is required, and removed when it no longer serves a product contract.

Historical compatibility is evidence to consider, not a veto against better engineering. Conversely, rewriting code merely to make it look new is not improvement. The strongest justified solution wins.

## Mercedes-Benz execution standard

The benchmark is Mercedes-Benz engineering excellence as an execution standard, not a branding claim or affiliation. The boot manager should feel calm and simple because difficult firmware, filesystem, discovery, trust and compatibility work has been engineered completely beneath the surface.

The product is not finished merely because firmware can launch it or because a boot entry starts successfully on one machine. Correctness includes failure, recovery, interruption, malformed input, unsupported platforms, trust boundaries, persistence and upgrade behaviour.

## Boot-specific engineering rules

### Determinism before cleverness

Boot is a critical path. Discovery order, configuration interpretation, fallback behaviour and state transitions must be explainable and reproducible. Hidden heuristics that can silently change boot meaning are unacceptable unless their authority, ordering and failure semantics are explicit.

### Failure is a first-class state

Missing, malformed, unsupported, untrusted, inaccessible and failed are not synonyms. The implementation must preserve meaningful distinctions rather than collapsing them into a plausible-looking boot option or silent fallback.

When the system cannot establish that an operation is safe or meaningful, explicit refusal is preferable to speculative success.

### Persistent mutation requires exceptional justification

Firmware variables, ESP contents, partition metadata, boot order and other persistent boot state are high-risk. A mutation must have a clearly owned reason, explicit preconditions, bounded scope, interruption behaviour, verification and recovery semantics proportionate to the damage a failure could cause.

Build machinery must never perform machine-state mutation as an accidental side effect of compiling or packaging.

### Read-only is the default for discovery

The act of discovering an operating system, filesystem or boot candidate should not modify it. Firmware filesystem integrations are read-only unless a separately documented product requirement proves that write capability is necessary and defines its safety model.

### One authority per concern

The project must converge toward one authoritative owner for each major concern: boot-flow policy, candidate discovery, configuration semantics, presentation/input, firmware interaction, filesystem access, trust policy, persistent mutation, build qualification and release identity.

Compatibility helpers may support an authority; they must not become a second policy engine.

### Bounded firmware work

Firmware is a constrained environment. Scanning, parsing, allocation, recursion, decompression and device traversal must be bounded by defensible limits. External configuration and on-disk metadata are untrusted inputs even when they reside on a local disk.

### Compatibility is deliberate

Support for a firmware quirk, legacy platform, filesystem or inherited toolchain must have an identifiable contract. Compatibility code that no longer protects a supported contract is debt, not heritage.

## Inherited implementation rule

Current paths and identifiers such as `refind`, `RefindPkg` and related names are transitional implementation details. They do not define the product's identity.

They should not be renamed in bulk merely for cosmetic consistency. They should disappear naturally when an engineering change establishes a clearer ownership boundary, removes obsolete machinery or replaces an inherited mechanism with a stronger product-owned implementation.

Provenance and licence notices must remain intact where required. Product identity and legal provenance are separate concerns and must not be conflated.

## Code and documentation standard

Substantive code commentary and maintained technical documentation are held to a postgraduate / university master's-level engineering standard. Comments should preserve invariants, ownership, firmware assumptions, protocol contracts, persistence semantics, trust boundaries, recovery reasoning and non-obvious compatibility constraints rather than narrating syntax.

Documentation must describe current truth. Historical imported documentation is retained as historical material and does not silently override maintained Infiltrator design documents.

## Build and release standard

Build and release machinery is production engineering. It must be deterministic, auditable and recoverable, with exact dependency identities where practical and no hidden workflow that rewrites product source or pushes unreviewed generated state to `main`.

Generated or vendored source must have one reproducible generation path and a separate verification path. CI verifies; it does not become an invisible second maintainer.

A release requires evidence appropriate to a boot manager: successful builds are necessary but insufficient. Firmware runtime behaviour, boot discovery, failure handling, filesystem integration, trust-sensitive paths and any persistent mutation require qualification proportionate to their risk.

## Final acceptance test

Before accepting meaningful work, ask:

- Is the behaviour owned by InfiltratorOS rather than inherited by accident?
- Is there one clear authority for the concern being changed?
- Are failure and recovery as deliberate as success?
- Can malformed or hostile input cause unbounded work or unsafe state mutation?
- Did the change reduce accidental complexity or clearly justify unavoidable complexity?
- Are compatibility and legacy mechanisms still protecting an explicit contract?
- Can another expert understand the invariants without reconstructing intent from history?
- Does the result make booting feel more trustworthy, calm and deliberate?

If not, the work is not finished simply because it builds.
