# Additive Common V2 support — development status

The existing EFI driver loading, binding, discovery, graphical interface,
installation directories, Windows and Linux boot paths remain unchanged.
The Common V2 implementation is an additional path, not an EFI driver
replacement. The current new source is deliberately isolated from the
production BOOT link until its security and integration requirements pass.

## Implemented source

- Restricted AMD64 SysV ELF mapping, relative relocation, exact V2 symbol
  lookup, rejection of other public exports and external dependencies, and
  final page permissions. Image-range queries reject unmapped padding and
  enforce the actual permissions, including RELRO.
- Canonical V2 descriptor and group inspection using the Libraries header.
  Every known group table and non-null callback pointer is checked against
  the authenticated image. A PROBE-only module is not a bootable reader.
- Bounded storage access, validated geometry, media identity invalidation,
  memory lifetime and checked write lease/durability hooks. The generic
  storage tests use disposable memory and test lease and failure paths.
- A read-only UEFI Disk I/O adapter. Existing EFI Simple File System handles
  take precedence. Unknown physical geometry fails closed. No writable
  firmware lease is claimed.

## Test and release boundaries

Run the host scripts with `COMMON_INCLUDE_DIR` pointing to the exact
Libraries include directory. Development CI pins Libraries commit
`225e640ebed5351b1481bfbeef1a6144ca5146e7` and checks the header Git blob
`90730b68a53ecf6a6e304c789501f29c17c728c7`. The test-only authentication
callbacks do not establish production trust; fixtures do not claim real
filesystem mounting.

The development workflow compiles the adapter against GNU-EFI and tests
the isolated components. Pushes explicitly marked `[v2-dev]` suppress the
existing automatic release job; the normal release process is unchanged
for ordinary pushes and manual invocation. No development build is
promoted as an installable BOOT release by this workflow.

Still required: actual signature policy and UEFI memory attribute adapter,
module discovery, volume arbitration and protocol registration, filesystem
namespace operations, qualified firmware writes, provider-by-provider
media and QEMU/OVMF boot tests, installer integration and release.
