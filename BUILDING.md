# Building InfiltratorOS Boot Manager

This is the maintained build entry point for InfiltratorOS Boot Manager.

`BUILDING.txt` is retained historical upstream documentation. It may still contain useful low-level information, but it is not the authority for current Infiltrator engineering policy or qualification claims.

## Current build system

The current tree contains three inherited build paths:

- GNU-EFI;
- older TianoCore/UDK-style make targets; and
- EDK2-oriented targets and metadata.

The existence of a target does not mean that target is currently release-qualified.

The repository's current automated firmware build evidence is focused on the GNU-EFI x86-64 path used for the InfiltratorFS UEFI integration. See `docs/VALIDATION.md` for what that does and does not prove.

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

The generic `make all` target currently detects locally installed EDK2/TianoCore paths and otherwise falls back to GNU-EFI. That behaviour is inherited and convenient for development, but host-dependent toolchain selection must not be mistaken for a deterministic release definition.

Release qualification should invoke an explicit toolchain/target and record the environment used.

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
