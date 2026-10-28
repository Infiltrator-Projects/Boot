# BOOT: universal filesystem `.so` loader — target contract

**This is the intended final architecture, not a claim that the current BOOT executable implements it.** The authority is [Filesystems' unified module contract](https://github.com/Infiltrator-Projects/Filesystems/blob/main/docs/UNIFIED_FILESYSTEM_MODULE_ARCHITECTURE.md).

BOOT is the **firmware-facing HAL and generic loader** for one installed filesystem `.so` per format. It does not own or rewrite FAT, EXT, OFS or any other filesystem's interpretation. The filesystem module itself is the driver. The BOOT loader translates between the verified module ABI and UEFI's Block I/O, Driver Binding/Simple File System and boot-file services. The same module is consumed by applications and, with a separate generic bridge, the future kernel.

## Required loading pathway

1. Start BOOT as a firmware-supported UEFI application. Obtain read access to a trusted source of module bytes; initially this may be a verified identical staging copy of the filesystem's `.so` on a UEFI-readable ESP, because BOOT cannot read an inaccessible filesystem to obtain its first reader.
2. Validate the module without executing it: immutable format ID, version, architecture, ABI profile, section bounds, permissions/relocations, imports, digest/signature and local Secure Boot policy. Do not trust a `.so` filename alone.
3. Load the **one** format engine under a restricted shared execution profile or a well-specified internal module payload. BOOT provides generic memory, logging, bounded block I/O, cancellation and read-only boot-file callbacks. A standard Linux/glibc ELF shared object is *not* directly executable by UEFI `LoadImage`; a BOOT-owned loader/ABI is required.
4. Adapt validated read-only filesystem functions to UEFI protocols; bind to the appropriate BlockIo handles only, reject foreign formats before any write, and rescan without destructive global reconnects.
5. Locate, read and launch the kernel/initramfs using the selected module, verify handoff and preserve firmware trust; if the operating system requires this module for its root, ensure the OS's generic bridge and exact module are available in early boot.
6. Rollback on invalid/incompatible/failed module; preserve existing verified `.efi` driver loading as a **transition fallback** until replacement has equivalent tests.

## Invariants

- **One filesystem-specific distributed driver is the `.so`.** No separately installed `.efi` per filesystem is required in the final architecture. Loader/EFI executable and host services remain generic BOOT infrastructure.
- Do not maintain or bundle independent filesystem-specific parsing, FAT mutation, EFI filesystem cores or alternative definitions in BOOT.
- BOOT needs only bounded read capability for boot-file discovery by default, even though the same engine provides read/write/format/recovery/defrag to suitably authorised OS/app consumers.
- A Linux kernel module cannot be substituted for a UEFI executable; BOOT bridges the module semantics through a verified compatible mechanism, not by renaming binaries.
- Never silently modify firmware NVRAM, ESP, BootOrder, disk metadata or Secure Boot state to make integration appear successful.
- Preserve the distinction between loading a kernel **from** a filesystem and mounting that filesystem as the operating system's **root**.

## Qualification gates

Build a real restricted module that is byte-identical to the application-facing installed `.so`, run it under BOOT in OVMF against independently created FAT12/FAT16 images, prove correct detection, rejection, read/seek and kernel/initramfs integrity. Test malformed/truncated/malicious modules and firmware binding failures. Then verify secure-boot policy and supported physical UEFI systems. Root mounting and runtime read/write require independent kernel-side evidence. Until these gates pass, current `drivers_x64` UEFI drivers are a temporary tested mechanism, not a finished universal-loader implementation.
