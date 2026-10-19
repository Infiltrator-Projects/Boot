# Building Boot

This is the maintained build entry point. `BUILDING.txt` is retained upstream
history; it does not define current product ownership or release contents.

## GNU-EFI build

The maintained build uses GNU-EFI, GCC, GNU make and binutils on x86-64 Linux:

```sh
bash scripts/build-gnuefi.sh
```

The script locates the installed GNU-EFI files and builds the Boot application.
`GNU_EFI_PREFIX` may identify an isolated GNU-EFI SDK instead of `/usr`.
The build does not fetch, compile, vendor or package filesystem implementations.
Filesystem source and EFI driver builds are owned by Filesystems, under
`firmware/uefi/` and the filesystem-specific native adapters in that repository.

`bash tests/run-icon-directory-test.sh` builds the production image reader and
decoders into a host regression test using the same GNU-EFI SDK. It exercises
directory handles, real PNG loading, relative paths, retries and cleanup without
requiring a firmware boot.

Historical TianoCore/EDK2 application targets remain explicit alternatives;
their existence is not evidence of current release qualification. Filesystem
build targets and EDK2 filesystem components have been removed from Boot.

## Runtime components

Boot loads independently installed EFI drivers from the driver directory beside
its executable and consumes their standard UEFI interfaces. A Boot build and its
Debian package contain no filesystem driver, including ext4. Firmware-provided
filesystems remain available; access to other formats requires their separate
module. Updating a driver does not require rebuilding Boot.

## Packaging and publication

CI generates the Boot executable, fonts, artwork and default configuration, then
seals the runtime bundle into `infiltrator-boot_VERSION_amd64.deb`. The public
release contains that package and `SHA256SUMS.txt`. Additional EFI binaries and
filesystem driver files are rejected from both the bundle and the DEB.

OS, utility and volume icons ship once under `icons/`, matching `icons_dir icons`
in `boot.conf`. The reader opens that directory and loads leaf filenames from
its handle. Font atlases, the banner and selection graphics remain beside the
executable at their configured paths. Artwork attribution and licences remain
under `icons/`. The bundle and DEB reject misplaced or duplicated graphics.

Filesystems independently builds and packages its EFI module payload. Setup, or
an explicit local test, selects the modules to deploy to its chosen target.
The Boot package keeps an empty driver directory for independent modules.

## Installation and qualification

Building and packaging never write to an EFI partition or NVRAM. The historical
`make install` target invokes the inherited installer and is not a build step.
The packaged existing-installation updater remains a limited, separately
invoked operation. It replaces only `BOOTX64.EFI`; configuration, icons, fonts
and external filesystem drivers retain their existing deployed state. It does
not migrate an ESP to the layout of a newly staged DEB. Setup owns the future
complete installation and resource-migration transaction.

Compilation establishes build/linkage evidence. Runtime, driver interoperability,
Secure Boot and installation/recovery behaviour require their own evidence under
`docs/VALIDATION.md`. The root `VERSION` identifies each immutable release.
