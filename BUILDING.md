# Building Boot

This is the maintained build entry point for Boot.

`BUILDING.txt` is retained historical upstream documentation. It may still contain useful low-level information, but it is not the authority for current Infiltrator engineering policy or qualification claims.

## Current build system

The current tree contains three inherited build paths:

- GNU-EFI;
- older TianoCore/UDK-style make targets; and
- EDK2-oriented targets and metadata.

The existence of a target does not mean that target is currently release-qualified.

The maintained source workflow builds the complete GNU-EFI x86-64 program and the read-only InfiltratorFS driver, and reproduces the pinned vendor tree. See `docs/VALIDATION.md` for what that does and does not prove.

## GNU-EFI prerequisites

On the Ubuntu CI path, the build uses:

- `gnu-efi`;
- `gcc`;
- `make`; and
- GNU binutils.

The makefiles allow EFI include/library locations to be overridden for non-default installations.

## Build targets

The historical top-level `Makefile` currently provides targets including:

```sh
make gnuefi
make fs_gnuefi
make all_gnuefi
```

It also contains TianoCore and EDK2 targets retained by the current implementation.

Use the narrowest target that matches what is being qualified. A broad build is not automatically stronger evidence if it silently selects a different toolchain based on host state.

## Determinism

The default `make all`, `make fs` and `make gptsync` targets use GNU-EFI deterministically. TianoCore/EDK2 targets remain explicit developer choices. Recursive make calls preserve the selected make executable and flags.

Run `bash scripts/build-gnuefi.sh` on an x86-64 Debian-family build host. It resolves the installed GNU-EFI toolchain and builds without invoking installation. Generic cleanup removes repository outputs; it does not delete an external EDK2 workspace.

## Vendored InfiltratorFS source

The Boot Manager contains a pinned, read-only UEFI integration of the portable InfiltratorFS core.

Verify the committed vendor tree:

```sh
./scripts/vendor-infiltratorfs.sh --check
```

Intentionally regenerate it from the exact pinned commits:

```sh
./scripts/vendor-infiltratorfs.sh
```

The generator uses isolated temporary directories, verifies exact commit identities and writes the vendor tree only when invoked in update mode.

## Installation safety

The historical top-level `make install` target calls `refind-install`, which can modify the current machine's EFI System Partition and NVRAM. It is not a build step and must never be used as part of ordinary CI or package compilation.

Treat installation and firmware mutation as separate operations governed by `docs/SAFETY.md`.

## Qualification

After building, apply the evidence rules in `docs/VALIDATION.md`. Compilation proves only that the selected source/toolchain combination produced an artifact. It does not prove runtime boot behaviour, trust handling, persistent-state safety or compatibility on architectures/firmware that were not exercised.

## Product packaging

Boot currently has no qualified Debian package or published binary release.
The imported Debian/RPM package definitions are archived under
`docs/provenance/packaging/`; they describe the source project's historical
distribution and are not active Boot publishing definitions.

A future Boot package must use the current Boot identity throughout. Setup
will own the graphical installation transaction and the explicit installation
of qualified firmware artifacts. Building or packaging Boot must not install
it to the developer's EFI partition or alter NVRAM.

The root VERSION identifies source previews, independently of inherited engine compatibility. The source workflow publishes an immutable prerelease ZIP after build/vendor checks. It excludes generated firmware binaries from distribution until firmware, trust and mutation/recovery evidence is sufficient for a binary release.
