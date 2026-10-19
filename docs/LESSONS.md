# Engineering lessons from the Boot integration cleanup

Recorded on 19 October 2026 following the Boot/Filesystems separation and icon
layout changes. [Architecture](ARCHITECTURE.md), [building](../BUILDING.md),
[safety](SAFETY.md) and [validation](VALIDATION.md) own the corresponding product
contracts; this record explains the failures in reasoning that those contracts
must prevent.

## Keep the component boundaries intact

- Boot owns discovery, configuration, presentation, generic EFI driver loading
  and operating-system handoff.
- Filesystems owns filesystem implementations, EFI readers/adapters, their
  build tooling and operating-system filesystem modules.
- Setup owns the future complete installation transaction, including target
  selection and the deployment of selected independent components.

An inherited source-tree layout does not decide product ownership. Optional
filesystem support must not become embedded Boot source or a bundled driver
merely because a local test needs it. Preserve source, exact dependency identity
and licences when moving ownership.

A local experiment that stages a kernel, initramfs or module onto one user's
SD card is separate from the product build and installer. Machine-specific
staging scripts stay local. Generic automated regression tests and accurately
labelled hardware evidence remain useful project material. A successful test on
one machine does not turn its paths or device choices into installer defaults.

EFI readability and operating-system filesystem support are separate
capabilities. Reading a kernel from OFS does not establish that Linux can mount
an OFS root. The [cross-project integration contract](https://github.com/Infiltrator-Projects/Filesystems/blob/main/docs/BOOTABLE_FILESYSTEM_CONTRACT.md)
owns that distinction and the staged integration milestones.

## Investigate the reader before settling on a layout

Direct PNG lookup beside Boot fixed the observed icon-loading failure. That
established a working path; it did not establish the exact cause of the earlier
subdirectory failure or make duplicated graphics necessary.

Investigate path construction, relative-directory semantics, EFI handle
ownership and read/decode failures before treating a successful file placement
as the final product layout. A regression test should exercise the production
reader and the relevant failure model. Record the limits of that model.

Boot 0.1.22 opens the configured icon directory and then reads leaf filenames
through that handle. Its host tests load a real PNG, cover failures and retain
empty/dot-directory compatibility. They do not establish the exact historical
firmware failure or qualify the new layout on physical hardware.

## Treat configuration, assets and deployment as one runtime contract

The executable, configured paths and deployed assets must agree. Moving icons
inside a package and changing its default configuration does not migrate an
existing EFI installation.

The current operations have distinct effects:

| Operation | Effect |
|---|---|
| Build and package | Produce artifacts without changing an ESP or NVRAM. |
| Install or upgrade the DEB | Stage managed files under `/usr/lib/infiltrator-boot/` and install the helper. |
| Existing-installation deploy helper | Replace only the existing `BOOTX64.EFI`, retaining a checked backup. |
| Complete installation or layout migration | A future Setup responsibility, including target and component selection. |

The current helper preserves existing configuration, artwork and external
drivers. It does not move those assets, install a filesystem driver, provision
a blank system or change firmware boot order. Consequently, its binary update
does not by itself switch an existing installation to `icons_dir icons`.

Future resource-layout migrations need explicit ownership, compatibility,
verification and recovery alongside the executable update. Report whether files
were built, packaged, published, staged, deployed or actually booted; those
states are independently meaningful.

## Inspect the payload and name the quantity being counted

Release downloads, package files, source files, driver modules and complete
filesystem implementations are different quantities. Give the unit and scope
of every count. A source-file count is not a count of supported filesystems.

A Debian archive can conceal the same unnecessary contents as a loose release.
Inspect its actual file inventory and detect duplicated graphics while
preserving attribution and licences. Removing 78 mirrored PNG copies reduced
the Boot DEB from 172 regular files to 94; packaging alone did not remove them.

The public download policy belongs to [README](../README.md#public-release-asset-policy):
one DEB and checksum, with intermediate build products kept in CI. Boot's
payload contains its EFI application and support files; independent filesystem
drivers belong to Filesystems.

Describe inactive source precisely. State whether a named release builds,
packages or loads it. Inactive code can be substantial; inactivity alone says
nothing about completeness or qualification. An EFI adapter also does not
automatically supply the full read/write facilities of an operating-system
filesystem stack.

## Match confidence to the evidence

For Boot 0.1.22, the established evidence is:

| Evidence | Established scope |
|---|---|
| GNU-EFI build and PE checks | The x86-64 EFI application compiles and links with the expected image type. |
| Production-reader host tests | Directory handles, real PNG decoding, relative paths, retries, short reads and cleanup work under the tested EFI model. |
| Artifact inspection | The DEB contains 94 regular files, including 78 icons once under `icons/`, six font/theme PNGs at their existing paths and no filesystem drivers. |
| Publication verification | The GitHub release, APT index and downloaded APT DEB have matching SHA-256 identities. |

Physical boot with the separated filesystem-driver arrangement and new icon
layout remains unverified. Complete blank-system installation through Setup
remains future work. Successful builds, mocks and publication do not establish
either result.

Keep functional changes focused and record what changed since the last
physical test. Test the identified production artifact rather than creating a
divergent test version of Boot. Carry existing hardware observations forward
with their exact scope; later code changes require their own evidence. Preserve
the known working boot path through the separately owned deployment/recovery
contract, and communicate the remaining uncertainty before a hardware test.
