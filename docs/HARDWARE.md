# Hardware and firmware compatibility

## Scope

This document records the native build's firmware and hardware assumptions separately from the architectural intent. It does not claim exhaustive compatibility with every computer or operating system.

## Host, firmware and CPU

The presently published native `.run` requires an **x86-64** host and a GNU-EFI development environment. Its Linux path locates a mounted EFI System Partition, writes the BOOT-owned directory and manages UEFI NVRAM. A conventional GNU-EFI build can compile the firmware executable without modifying the current computer's ESP.

The release workflow compiles PE32+ x86-64 EFI images and verifies that `boot/boot_x64.efi` and all six required drivers exist. The QEMU/OVMF smoke test emulates a UEFI system; it is not a full physical-platform qualification. UEFI implementation differences, GPU display modes, firmware variables, storage-controller exposure, Secure Boot policy and driver-loading behaviour can affect real devices.

UEFI firmware boot selection is independent of BOOT's graphical list. Choosing a separate GRUB or vendor firmware entry from a machine's one-time boot menu does not depend on GRUB being visible inside BOOT.

## Filesystem access currently built

| Filesystem family | Current standard EFI-driver evidence | Qualification boundary |
| --- | --- | --- |
| EXT2 | `ext2_x64.efi` built and installed | Driver presence does not prove every EXT2 volume/feature combination |
| EXT4 | `ext4_x64.efi` built and installed | Real-machine direct-kernel boot has been observed on one configuration; not universal verification |
| ReiserFS | `reiserfs_x64.efi` built and installed | No complete hardware/filesystem matrix established |
| ISO 9660 | `iso9660_x64.efi` built and installed | Optical-image/controller behaviour not exhaustively validated |
| HFS family | `hfs_x64.efi` built and installed | Current HFS reader supports HFS+/HFSX cases; plain HFS may be recognised but rejected as unsupported |
| Btrfs | `btrfs_x64.efi` built and installed | Not every on-disk feature and topology is qualified |

`filesystems/` also contains additional source targets, including NTFS-related code, but that is **not part of the six mandatory drivers** of the published native installer. A source file or alternate make target is not equivalent to enabled, installed and validated boot support.

BOOT also relies on firmware-accessible EFI System Partitions to find the BOOT executable and other EFI applications. The ability to read a volume depends on the firmware and compatible loaded drivers. A filesystem type printed by probing may represent valid detection even when that specific variant is not readable; plain HFS is a known example.

## Direct kernel and OS booting

EFI applications such as Windows Boot Manager and GRUB may be launched through BOOT's EFI load path. A directly launched Linux EFI-stub kernel additionally requires compatible kernel image, appropriate startup parameters and, where needed, a matching initramfs/initrd. A menu icon proves only that an entry was generated; it does not prove the OS reached userspace.

The intended universal discovery policy must therefore coexist with explicit per-path readiness and firmware compatibility. The number of supported installed operating systems is not limited to an arbitrary predefined list, but unsupported filesystems and OS-specific boot methods remain genuine boundaries.

## Firmware state and safe recovery

The native install target is the ESP directory `/EFI/InfiltratorOS/BOOT`. The install operation intentionally does not delete `EFI/Microsoft`, `EFI/ubuntu` or `EFI/BOOT`. These independent boot paths may provide firmware-level recovery if BOOT is not selectable or cannot start a particular operating system.

Tests exercise protective checks for symbolic links, unexpected directory ownership, SHA-256 marker mismatches and ambiguous/duplicate firmware entries. They do not imply that all firmware vendors implement variable writes, Secure Boot or EFI handle binding identically.

## Secure Boot and hardware-specific evidence

The presence of keys, MOK code and signing-related branches must not be interpreted as proof that all Secure Boot configurations are validated. Release qualification must establish which firmware trust chains, image signatures, enrollment states and failure responses were actually tested.

A real-hardware result should record device model, firmware revision, Secure Boot state, boot selection, filesystem variant, kernel/initrd where applicable, console visibility and outcome. A successful boot on one machine is valuable but does not validate millions of other configurations.

## Common V2 distinction

The repository's current firmware access uses EFI filesystem driver binaries and EFI protocols. It does not establish a completed Common V2 `.so` loading, authentication or execution pathway. Such future compatibility must be demonstrated against the actual V2 specification on each target build profile, including freestanding UEFI constraints.
