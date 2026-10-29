# BOOT filesystem shared-object loader

BOOT loads the **same `FAT16.so` bytes** used by Defragmenter and the application
filesystem interfaces. The x86-64 GNU-EFI implementation is in
`refind/so_elf.c` and `refind/so_uefi.c`. The loader contains no FAT reader or writer.
The per-format repository owns the engine; Boot owns ELF loading, host services,
UEFI protocols, discovery and OS handoff.

## Module and bootstrap contract

Stage an identical copy of `/System/Filesystems/FAT16.so` under
`filesystems_x64/FAT16.so` beside Boot's EFI executable, on an already readable
bootstrap volume. The copy is needed to obtain the first filesystem reader.
It is the application-facing shared object, not a rebuilt firmware variant.
Boot packages no filesystem module and does not create or update this staging
copy. Selected component deployment remains a separate operation.

The shared C contract is `include/ifs_boot_module.h`, mirrored verbatim from
`Filesystem-FAT16/native/include/infiltratr/fs/boot_module.h`; integration tests
compare the files. The module exports `infiltrator_filesystem_boot_v1` and a
fixed `.ifsboot` manifest. The profile is little-endian x86-64 ELF ET_DYN,
SysV function and callback calls, no red zone, constructors, TLS, imports or
host libc, and relative relocations only. Defragmenter's existing host table
and V1/V3/V4 filesystem exports remain in that same object, untouched in layout.

Boot validates program/section tables, file and virtual ranges, non-overlapping
segments, declared permissions, format/ABI identity, dynamic metadata, symbols
and relocation destinations/addends before allocation or execution. Limits are
16 MiB of input, 32 MiB per loaded image, 16 load segments, 8 staged modules,
256 directory entries/device handles considered and 32 mounted volumes.
Each mount receives a separate relocated image because the engine owns one
active mount per image. File contexts are bounded to 16 KiB each.

## Read-only UEFI adaptation

The generic adapter supplies bounded, media-ID-checked BlockIo reads with an
aligned 64 KiB bounce buffer. It publishes a child handle containing read-only
BlockIo, SimpleFileSystem and a distinct device path. The module owns path
lookup, directory enumeration, file data and volume-label interpretation.
Boot owns UTF-16/UTF-8 conversion, volume-relative path normalisation, EFI file
positions/information and protocol lifetimes.

Firmware's original handles and protocols remain installed. Boot's own volume
scan selects the SO child instead of its parent, avoiding duplicate candidates.
The bootstrap volume is retained on its original reader. Module images and
published protocols remain alive through Linux EFI-stub initramfs loading;
closing Boot's menu handles must not unload them before ExitBootServices.
Media errors remain errors; this adapter never retries through another parser.

An explicit test or deployment entry can require the actual module binding:

```text
menuentry "Linux through FAT16" {
    volume YOUR_VOLUME_LABEL
    filesystem_module fat16
    loader \\vmlinuz
    initrd \\initrd.img
    linux_load memory
    ostype Linux
    options "YOUR_EXISTING_LINUX_OPTIONS"
}
```

`filesystem_module` matches the bound module's immutable format ID. Missing,
foreign or rejected modules disable the entry; launch checks the handle again.
Submenu copies retain the requirement. Existing entries without this directive
retain their normal behavior. Volume labels are preserved, not renamed to make
the experiment pass. Existing independently installed EFI drivers remain a
transitional compatibility path for other volumes.

## Trust and permissions

Unsigned ELF loading is allowed only when SecureBoot is disabled or the
firmware reports that the variable is absent. An enabled value, malformed
value or read error rejects modules before execution. This is not a Secure
Boot signature-verification implementation. Kernel EFI image authentication
continues through the existing firmware handoff.

The ELF reader always rejects writable executable segments and executable
stacks. When UEFI's Memory Attribute Protocol is available, Boot applies
read-only/code/data execution permissions and refuses a reported protection
failure. Older firmware without that protocol is supported without claiming
hardware page-permission enforcement. A loaded filesystem module is trusted
privileged code, not a sandbox. See UEFI 2.10/2.11 section 37.7.1.

No module scan or boot test changes host NVRAM, an ESP, BootOrder or filesystem
metadata. Firmware read-only access does not remove the module's application
write, format, defragmentation or recovery functionality.

## Established evidence and remaining scope

`tests/run-so-tests.sh` exercises the production ELF loader and UEFI adapter
against independently formatted media: nested files, long/Unicode paths,
directory buffers, seek/EOF, read-only refusal, media/range errors, SecureBoot
policy and malformed ELF rejection. The host applies actual page permissions.

`tests/test_so_boot.py` boots the production Boot EFI in OVMF, reads a genuine
Linux EFI-stub kernel and initramfs from FAT16 through the exact module and
reaches an initramfs marker. It checks byte-for-byte media non-mutation. A
rejected-module run must fail to boot even though native firmware FAT support
is still available. Defragmenter compatibility separately exercises the same
module through its unchanged production host, including write/recovery tests.

This qualifies the tested x86-64 GNU-EFI/OVMF path. Physical Dell qualification,
signed-module loading and a kernel-side SO bridge remain separate work. Booting
a kernel from FAT16 does not establish Linux root mounting through the SO.
