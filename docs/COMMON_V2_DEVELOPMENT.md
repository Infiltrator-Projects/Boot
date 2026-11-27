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
  registration. The existing scanner and launch path consume the resulting
  EFI protocols; no new format-specific code is present.
- File/directory path conversion, independent per-open positions, directory
  buffer-size retry, file and filesystem information, explicit mutation
  rejection and unmount/handle cleanup. CORE lacks physical allocation and
  timestamps: those EFI metadata values are left unknown (zero).
  Symbolic-link opens are not supported by this initial adapter.
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

## Evidence as of development run 45927816873

Independent EXT4 and NTFS firmware payloads have successfully passed real
disposable-media tests through the actual BOOT ELF mapper, descriptor, storage
host and provider callbacks at both 512 and 4096-byte logical sector sizes.
This verifies probe, mount, directory enumeration, stat, byte-exact reads and
unmount with unchanged disk images. The original EFI menu passed OVMF.
The signed firmware integration test did not pass: the tested Debian OVMF
Secure Boot image did not expose the memory-attribute protocol. Its documented
VM configuration now explicitly retains that protocol. This does not change
production BOOT's fail-closed memory permission requirement.

The 22-provider build and ELF/descriptor inspection matrix tests pinned
source revisions individually. A passing matrix cell establishes structural
compatibility, not a usable filesystem namespace or an operating system boot.
Full media/firmware qualification is separate.

Pushes marked `[v2-dev]` do not publish an installable release. Production
publication requires successful qualification and authorization of the exact
commit. Remaining work includes signed firmware execution and OS launch,
individual provider real-media and packaging qualification, UEFI exclusive
write ownership and durability (the firmware path remains read-only), and
final release publication. No passed host test is a substitute for these
release conditions.
