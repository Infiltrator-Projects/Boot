# Safety and Trust Boundaries

## Scope

A boot manager executes before the operating system can provide its normal process isolation, storage protections, service supervision or recovery tools. Safety therefore has to be designed into the boot manager itself.

This document defines the product safety rules for firmware interaction, persistent state, filesystems, configuration, trust-sensitive mechanisms and dependency integration.

## General rule

Observation and discovery should be non-destructive. Mutation is exceptional.

Reading devices, enumerating firmware handles, parsing configuration, discovering loaders and presenting boot choices must not alter durable machine state as a side effect. Any operation that can alter NVRAM, boot order, ESP contents, partition tables, filesystem metadata or other persistent state must be separately owned and explicitly invoked.

## Persistent mutation

A persistent mutation is acceptable only when all of the following are defined:

1. the product requirement that justifies it;
2. the exact state that may change;
3. preconditions proving the operation applies to the intended target;
4. the authority under which it runs;
5. interruption and partial-failure behaviour;
6. post-operation verification;
7. recovery or rollback where technically possible; and
8. user-visible failure semantics.

Build, test and packaging steps must not mutate the developer or CI machine's boot configuration merely as a consequence of producing an artifact.

## Firmware variables

Firmware variables and boot-order state are shared persistent platform resources. Code that changes them must minimise the write set, preserve unrelated entries, distinguish write failure from verification failure, and avoid repeated writes when the desired state is already present.

A read failure must not be interpreted as an empty value unless the firmware contract proves that equivalence.

## Filesystems

Firmware filesystem access is read-only by default. The boot manager needs to discover and load boot material; it does not need general-purpose write capability to perform that job.

Filesystem parsers operate on untrusted on-disk structures. They must bound lengths, counts, offsets, recursion, decompression and allocation. Corrupt metadata must produce explicit failure rather than unchecked pointer arithmetic or unbounded traversal.

Filesystems owns EFI filesystem implementations, their pinned source and parser
qualification. Boot owns safe use of the exposed EFI protocols, including
bounded reads and path/handle ownership. Importing a format implementation into
Boot to serve a local test would cross this boundary. Firmware readers remain
independent of host operating-system runtime services.

## Configuration and external data

Configuration files, loader metadata, filenames, device labels, filesystem metadata and firmware-provided strings are untrusted inputs.

Parsers must validate structure before use, preserve meaningful invalid/unsupported states, reject unsafe sizes or encodings, and avoid allowing presentation text to influence executable or persistent policy implicitly.

## Boot candidates

Discovery does not establish trust. A candidate can exist yet still be unsupported, inaccessible, malformed or untrusted.

The product must preserve those distinctions through selection and handoff. A failure to establish required trust may not be converted into ordinary success by changing the label shown to the user.

## Secure Boot and MOK-sensitive paths

Changes involving Secure Boot, signature verification, shim interaction, MOK state or equivalent trust material require separate qualification. Trust decisions must occur before execution is authorised and must remain diagnosable afterward.

The presence of inherited trust-related code is not evidence that its current boundary is ideal. New work must move toward one explicit trust authority rather than spreading verification policy through scanning, UI and launch code.

## GPT/MBR synchronisation and disk metadata

`gptsync/` is materially more dangerous than ordinary boot discovery because it can affect persistent partition metadata. It must be treated as a distinct mutation-capable utility even where historical build machinery groups it with the boot manager.

Changes in that area require explicit target verification, interruption analysis and recovery testing appropriate to partition-table mutation.

## Dependency and vendoring safety

Vendored source must be pinned to exact source identities and accompanied by applicable licence material. Generation must occur in unique temporary locations, clean up after itself, and verify that the fetched commit is exactly the requested commit.

CI verifies that committed vendor state matches the deterministic generator. CI must not silently regenerate vendor source, commit it and push it to `main` as a side effect of verification.

## Resource safety

Firmware resources are finite. New code must reason explicitly about allocations, handle/protocol ownership, repeated enumeration, recursion depth, buffer growth and decompression expansion.

Where a hard upper bound can be derived from a format or protocol, enforce it. Where the platform supplies a length, validate it before allocation or arithmetic.

## Failure and recovery

Safety-relevant failure should leave the system no worse than before the operation wherever technically possible. If that cannot be guaranteed, the operation must document the remaining failure window and provide a recovery path proportionate to the risk.

A failure message is not a recovery strategy. Recovery means a defined way to restore a bootable or otherwise known state.

## Review trigger

Any change that introduces a new write to persistent state, broadens filesystem authority, changes trust decisions, adds a new firmware protocol dependency, changes partition metadata, or alters the boot handoff boundary requires an explicit safety review against this document before release.
