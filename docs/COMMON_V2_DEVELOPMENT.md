# Additive Common V2 support — development status

BOOT retains its original rEFInd-derived EFI driver loader and filesystem
drivers. The additional AMD64 path is compiled by setting
`COMMON_V2_INCLUDE` to the pinned Libraries include directory. The generic package and native installer now compile this path against the pinned
headers. A legacy build without the include directory remains available for regression
testing; it must not be represented as a Common V2 release.

## Architecture and implemented source

- Exact raw ELF AMD64 SysV loader, relative relocations, bounded segments,
  immutable descriptor/group checks, V2-only public export, and verified
  page permissions. Unsupported dynamic features fail closed.
- SHA-256 integrity verification of the exact input bytes against explicitly
  enrolled raw SHA-256 entries in the platform's authenticated `db`
  variable; matching `dbx` entries revoke permission. Both databases must
  be readable and authenticated, and Setup Mode must be disabled. This is
  not an Authenticode PE digest or MOK-only trust. An adjacent checksum is
  not authority. Firmware trust enrollment is not automatic.
- Discover the raw freestanding ELF files in the existing `drivers_x64/`
  directory, derive the expected canonical format identifier from their
  names, and keep a sorted boot-session registry. A hosted Linux carrier
  with an embedded `.infiltrator_boot` section is not directly executable;
  its qualified payload must be extracted during installation.
- Generic, bounded partition-relative Disk I/O; verify media identity and
  derive physical geometry from the exact parent device path because UEFI
  requires partition physical-geometry fields to be zero. Unknown parent
  geometry and unsupported virtual device paths are rejected.
- Read-only UEFI Simple File System publication after existing EFI bindings
  have had first refusal. Generic V2 PROBE, MOUNT, STAT, LIST and READ are
  required, and a successful root-directory stat is required before SFS
  registration. All eligible providers are probed before a native mount;
  overlapping successful claims are rejected rather than choosing an arbitrary
  owner. The existing scanner and launch path consume the resulting EFI
  protocols; no new format-specific code is present.
- File/directory path conversion, independent per-open positions, directory
  buffer-size retry, file and filesystem information, explicit mutation
  rejection and unmount/handle cleanup. CORE lacks physical allocation and
  timestamps: those EFI metadata values are left unknown (zero).
  Where a provider exposes links through CORE and METADATA.READLINK, the
  namespace adapter resolves relative and absolute targets with bounded hops,
  root confinement and UTF-8 validation. Providers without a suitable link
  facility cannot claim generic link resolution.
- Common V2 modules execute natively with firmware privileges. Signature
  trust and mapping checks are not process isolation: arbitrary faults in
  authenticated code cannot in general be contained by this loader.

## Validation and release boundaries

Development CI pins Libraries commit
`225e640ebed5351b1481bfbeef1a6144ca5146e7` and verifies its canonical ABI
header Git blob `90730b68a53ecf6a6e304c789501f29c17c728c7`. The host test
fixtures validate specific code contracts, not real filesystem bootability.
The development workflow compiles the firmware adapters with GNU-EFI and
links the feature-enabled BOOT alongside the original six EFI drivers.
Its actual run result is authoritative; the existence of a workflow or a
test does not prove it passed.

## Installation contract

The existing native RUN and generic AMD64 package stage modules from
`/System/Filesystems`. The host verifier recognizes either a raw firmware ELF
or a hosted ELF carrier with a single `.infiltrator_boot` section. It authenticates
the exact extracted bytes against platform db/dbx and uses the same structural
loader with non-executable host mappings; it never executes provider code on
the installation host. The firmware independently authenticates and validates
the descriptor before execution. Only accepted payloads enter
`/EFI/InfiltratorOS/BOOT/drivers_x64/`. Existing EFI drivers remain present.
Neither installer changes the platform trust database. A new module's raw
digest must be explicitly authorized by the platform owner; a hosted package's
APT signature or the local MOK certificate alone does not authorize raw ELF.

## Evidence and outstanding qualification

Development runs [45942919452](https://github.com/Infiltrator-Projects/Boot/actions/runs/45942919452)
and [45943021441](https://github.com/Infiltrator-Projects/Boot/actions/runs/45943021441)
both completed with all 26 jobs successful (25 providers and the BOOT
host/firmware job). These results apply to the respective tested revisions,
not automatically to later commits.

The independent EXT4 and NTFS firmware payloads passed real disposable-media
tests through the BOOT ELF mapper, descriptor, storage host and callbacks at
512 and 4096-byte logical sector sizes. Independent media fixtures for EXT2,
EXT3, Btrfs, XFS, FAT12, FAT16, FAT32, HFS, HFSPlus, exFAT, Minix and BFS also
passed. These tests cover actual namespace reads and unchanged media, not
every filesystem feature.

The signed EFI child and signed Linux EFI-stub kernel plus independent
initramfs executed from native NTFS under enrolled Secure Boot OVMF, with
the original EXT4 EFI reader coexisting. Authenticated malformed, revoked,
and untrusted modules were rejected. The overlapping-claim fixture passed
without mounting the ambiguous native partition or launching its child.
Both BOOT packaging safety tests passed. The OVMF fixture explicitly retains
the standard memory-attribute protocol; production BOOT still rejects native
execution without enforceable permissions.

The 25-provider matrix establishes structural ELF and descriptor
compatibility of pinned source revisions. This is not equivalent to
independent real-media or OS boot qualification for all 25 providers.
Separate provider carrier checks passed for FAT12/16/32, OFS, EFS, BFS and
QNX4 at the tested revisions. The QNX4 check uses its production carrier
build, not an installed Debian package.

The subsequent bounded namespace/link changes, direct native EXT4 signed
EFI and Linux tests, and expanded production-carrier checks have been
committed. Consult their exact workflow results before claiming success.
The expanded carrier checks preserve the owning project's pinned Common
dependency and authenticate the actual embedded image using disposable
trust fixtures before exercising the BOOT loader.

Pushes marked `[v2-dev]` do not publish an installable release. Production
publication requires successful qualification and authorization of the exact
commit. Further work includes remaining independent providers, complete
real-media and boot coverage, the InfiltratorFS 0.20 production adapter,
firmware-exclusive write ownership and durability (the path remains read-only),
and release qualification. Swap has no file namespace and is not an OS
boot reader. Documentation-only repositories cannot be counted as working
filesystem implementations. No old InfiltratorFS format or experimental
format-20 prototype is a substitute for a production 0.20 implementation.
