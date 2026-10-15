# Product Quality

## Standard

InfiltratorOS Boot Manager is held to the canonical Infiltrator Engineering Philosophy and its Mercedes-Benz execution benchmark: deep engineering, restrained presentation, reliability, trust, technical precision and craftsmanship as one integrated standard.

Quality is not a final polish pass. For a boot manager, correctness, startup behaviour, input handling, visual clarity, firmware compatibility, filesystem safety, trust handling, build reproducibility and recovery are all product quality.

## Acceptance principles

### Correctness is broader than successful boot

A feature is not complete because the common path works. Acceptance includes malformed configuration, absent devices, unsupported filesystems, inaccessible loaders, allocation failure, interrupted operations, firmware quirks and any relevant trust failure.

### Simplicity must be earned

The ordinary experience should be simple, but internal complexity may remain where the domain genuinely requires it. Reduction targets accidental machinery: duplicated policy, unnecessary compatibility layers, hidden state, repeated parsing, redundant scanning and dead branches.

### One product voice

User-visible terminology, maintained documentation, diagnostics and new source-level abstractions should describe InfiltratorOS Boot Manager in InfiltratorOS terms. Historical terminology remains only where it is part of a current technical or legal contract.

### Performance is observable behaviour

Firmware-stage delay is highly visible. New work must avoid unnecessary startup cost and should preserve enough observability to identify where time is spent. Performance changes may not trade away deterministic behaviour, safety or useful diagnostics merely to improve a headline number.

### Failure presentation is part of usability

An error should identify the failed concern and preserve meaningful state. Silent fallback is acceptable only when fallback is an explicit product rule and the original failure remains diagnosable.

### Accessibility and input consistency still matter

Firmware environments impose limitations, but keyboard navigation, focus indication, selection state, readable contrast, timeout behaviour and predictable input remain quality requirements within those limits.

## Code quality

New and substantially modified code must:

- have a clear owning concern;
- make lifetimes and firmware-resource ownership explicit where non-obvious;
- avoid creating a second source of policy truth;
- preserve meaningful failure states;
- bound work performed on untrusted configuration or on-disk data;
- isolate platform quirks from general product semantics;
- explain non-obvious firmware, ABI, protocol, trust and recovery constraints in technically substantive comments; and
- remove obsolete code when its contract has genuinely ended.

Large functions or files are not rejected solely for size. They are rejected when size reveals unrelated ownership or makes state transitions impossible to reason about confidently.

## Documentation quality

Maintained documentation must describe the current system, not an aspirational fiction and not merely its historical origin. A competent engineer should be able to determine product intent, ownership, major control flow, failure semantics, safety boundaries, supported build evidence and provenance without reconstructing them from commit history.

Historical imported documents remain useful evidence but are not maintained product authority.

## Build and dependency quality

Build machinery must make dependency identity and artifact provenance inspectable. Vendored source must be reproducible from exact pins and independently verifiable. CI must verify repository state rather than silently creating new source state and pushing it into the product branch.

A build path is considered supported only to the extent that it is intentionally maintained and qualified. The existence of inherited make targets does not itself establish a support promise.

## Release quality

A release candidate must have evidence proportionate to what changed. At minimum, changes affecting boot flow, discovery, filesystem integration, trust or persistent mutation require validation beyond source compilation.

No release is considered Mercedes-Benz quality merely because all automated checks are green. Automation is evidence, not judgment.

## Definition of finished

Meaningful work is finished only when:

- the behaviour is correct on success and failure paths;
- ownership is at least as clear as before;
- the change does not introduce an unnecessary second authority;
- compatibility and fallback behaviour are explicit;
- documentation and comments preserve non-obvious engineering reasoning;
- validation matches the risk of the change; and
- the result makes the product more coherent rather than merely more different.
