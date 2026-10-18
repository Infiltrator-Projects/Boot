# OFS EFI reader: pinned canonical Filesystems code

This experimental firmware adapter consumes the **project-owned** Amiga OFS
portable reader and core from `Infiltrator-Projects/Filesystems`, commit
`768a42d00e17f2329fd9d3d091c41eca9fd36ff4`.

The six files under `filesystems/ofs/vendor/` are copied verbatim from:
- `native/filesystems/ofs/efi/reader.{c,h}`
- `native/filesystems/ofs/core/ofs_core.{c,h}`
- `native/filesystems/ofs/core/ofs_primitives.{c,h}`

The on-disk implementation is owned by Filesystems. Boot's
`filesystems/fsw_ofs.{c,h}` only adapts the portable reader to UEFI Disk I/O
and the standard EFI Simple File System protocol via Boot's existing FSW
integration. Update all pinned files atomically from a reviewed Filesystems
revision and run the Filesystems portable tests.

Current restrictions: read-only, 512-byte logical sector, DOS\0 OFS on
a partition-relative BlockIo volume, no RDB discovery, no variant fallback,
no symlink emulation. No physical boot qualification yet.

Build explicitly with `make -C filesystems ofs_gnuefi` and a compatible
GNU-EFI toolchain. **This experimental driver is NOT in the normal release
manifest and must not be enabled until OVMF plus physical boot qualification.**
