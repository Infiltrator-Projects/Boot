# Design

**Filesystem direction:** one authoritative per-format `.so` for BOOT, kernel and applications; BOOT owns the generic UEFI adapter, not a separate filesystem engine. See [loader contract](FILESYSTEM_SO_LOADER_CONTRACT.md). Firmware defaults to read-only boot access; this does not limit the shared engine's read/write functionality for other consumers.

## Product intent

Boot should make firmware-stage boot selection feel deliberate, calm and dependable. The user should see the smallest amount of information necessary to understand what can be booted, what is selected, and what went wrong when boot cannot proceed.

Complexity belongs beneath the interface. The product may contain substantial firmware, filesystem, compatibility and trust machinery, but the user should not be required to understand that machinery during normal boot.

## Design principles

### Boot first

Every visible feature and internal mechanism must justify its place against the primary job: reliably selecting and starting an operating system. Utilities, diagnostics and compatibility mechanisms must not distort the ordinary boot path.

### Predictable discovery

The same machine state and configuration should produce the same candidate set and ordering. Discovery rules must be deterministic enough to explain. Heuristics may assist discovery, but they must not become hidden, unstable product policy.

### Calm presentation

The interface should emphasise recognised boot choices, the current selection and actionable status. Noise, duplicated metadata, unexplained firmware terminology and permanent exposure of advanced mechanisms work against the product.

### Progressive disclosure

Advanced diagnostics, compatibility actions and exceptional recovery mechanisms should be available when needed without making the ordinary boot surface feel like a firmware administration console.

### Visible uncertainty

The interface must not present guessed state as fact. Unsupported, inaccessible, malformed and untrusted candidates should be represented truthfully or omitted according to explicit policy, with enough diagnostic information available to explain why.

### Fast by engineering, not by omission

The startup path should avoid unnecessary scanning, repeated parsing, redundant device work and decorative delay. Performance optimisations must preserve correctness and inspectability.

### No surprise mutation

Simply opening the boot manager, navigating menus or inspecting a candidate must not change firmware variables, disk metadata, boot order or filesystem contents. Any operation with durable effects must be explicit and separable from ordinary selection.

## Configuration design

Configuration is product policy expressed as data. It must be parsed once into validated meaning and then consumed consistently.

Configuration handling should:

- distinguish absence from invalid syntax;
- reject or isolate unsupported values without corrupting unrelated settings;
- define precedence explicitly where defaults, discovered state and user configuration interact;
- avoid multiple parsers or ad hoc interpretation in feature modules;
- preserve a clear path back to safe defaults; and
- keep historical compatibility syntax only where it protects an explicit supported contract.

A compatibility parser may translate old syntax into the current internal model. It must not force the current internal model to remain organised around historical syntax forever.

## Boot-candidate design

A boot candidate should be represented as validated evidence plus explicit policy, not as an arbitrary bundle of UI fields.

The design should keep distinct:

- where the candidate came from;
- what loader or executable it identifies;
- the filesystem and device needed to access it;
- the trust/verification state known about it;
- whether the candidate is supported;
- the display identity shown to the user;
- ordering/default-selection policy; and
- the actual handoff mechanism used to launch it.

This separation prevents presentation, scanning and launch code from silently redefining one another's semantics.

## Filesystem design

Firmware filesystem support exists to locate and read boot material. It is not a general filesystem service.

Read-only access is the default because it sharply reduces failure and corruption modes. A writable firmware filesystem path would require a separate product requirement and safety design before implementation.

Filesystem implementations and their platform dependencies are maintained in Filesystems. Boot consumes their UEFI protocols and does not embed their portable cores or implement filesystem directory traversal and file reads.

## Compatibility design

Compatibility code is justified by a supported machine, firmware behaviour, filesystem, loader or trust scenario. Every compatibility mechanism should answer two questions:

1. What supported contract would fail without this code?
2. What evidence proves the workaround is still required and correctly bounded?

Compatibility that cannot answer those questions is a candidate for removal.

## Visual and interaction quality

Firmware constraints do not excuse careless interaction design. Within the capabilities of the environment, the product should maintain consistent hierarchy, spacing, iconography, focus indication, keyboard behaviour, timeout behaviour and error presentation.

The user should be able to identify the default action immediately. Destructive or persistence-changing operations must be visually and behaviourally distinct from ordinary boot selection.

## Product identity

The project must be understandable as Boot without requiring knowledge of the codebase it originally started from. Historical filenames and symbols may remain during migration where changing them would create disproportionate risk, but new product concepts, documentation, UI language and architectural decisions must use InfiltratorOS terminology and ownership.

The target is not a reskinned historical implementation. The target is a coherent InfiltratorOS boot product that retains any existing mechanism only when that mechanism remains the strongest engineering choice.
