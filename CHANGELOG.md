# Changelog

## 0.1.34 — 2026-10-23

- Add opt-in `storage_x64/` and `storage/` UEFI storage-provider loading before the filesystem `drivers_x64/` stage; missing directories change nothing on normal computers.
- Keep Boot filesystem-neutral and PCI-controller-neutral: native storage drivers self-bind to matching devices and expose standard EFI BlockIo handles; OFS and other filesystem drivers continue consuming standard EFI protocols.
- Do not ship or automatically install an unqualified Realtek driver. The Dell RTS525A 30-second firmware failure remains open pending a real controller driver and physical qualification.

## 0.1.33 — 2026-10-22

- Correct and subdivide startup diagnostics without changing boot search policy. The old 'Graphics' time included screen setup and icon loading, while its 'OS scan' time combined bootloader discovery, tool discovery and final menu setup.
- The same four diagnostic rows now distinguish UI/icons, loader scan and tools/menu using existing RTC marks. This identifies which portion of the observed 151-second pre-menu delay actually blocked.
- Preserve boot entries, automatic discovery, volume protocols, firmware variables, graphics behaviour and EFI file loading. Physical timings and the Linux OFS handoff remain unverified for this build.


## 0.1.32 — 2026-10-22

- Show approximate elapsed time since the first OFS/Linux EFI file read, average confirmed KiB/s and estimated seconds remaining when enough bytes and time have accrued. Read-only RTC sampling occurs after completed EFI reads, without a watchdog, background timer or firmware-state writes.
- On a failed file transfer, show elapsed time and verified bytes before leaving through the existing error path. Guard invalid RTC readings and discontinuities rather than claiming precise timing or live updates during a blocked firmware call.
- Record the physical Dell Latitude 5550 OFSBOOT test using Boot 0.1.31 and Filesystems 0.5.51: the external EFI driver returned Device Error at partition-relative sector 125093997, after 1,441,792 bytes were successfully read. Actual handoff remains unverified.


## 0.1.31 — 2026-10-22

- Respond to the Dell's memory-first OFS test remaining at 0%: report the first completed 64 KiB EFI file read, subsequent completed progress at 256 KiB intervals, and EOF, rather than hiding transfers until five percent.
- Mark entry into the initial synchronous EFI file read so a blocked first call can be distinguished from later transfers. Preserve firmware loading, validation, EFI ownership and error handling unchanged.
- The physical Dell OFS kernel handoff remains unverified; diagnostic display changes do not imply a firmware-I/O fix.

## 0.1.30 — 2026-10-22

- Add `linux_load memory` for explicitly configured manual Linux EFI-stub menu entries; default `linux_load path` stays unchanged for every other loader.
- When selected, read the complete kernel from the selected EFI filesystem through verified 64 KiB chunks and report progress *before* calling firmware memory-backed `LoadImage`. Avoid the potentially blocked first firmware device-path LoadImage observed on a Dell Latitude 5550 with OFSBOOT.
- Retain the original device path for Linux `initrd=` access and standard firmware image/security validation. Fail closed for invalid manual modes and non-Linux targets; maintain entry/submenu policy, independent filesystem drivers and normal boot order.
- Full physical Dell OFS boot remains unverified.


## 0.1.29 — 2026-10-22

- Show explicit firmware device-path loading, EFI filesystem fallback, memory-image loading, and Linux handoff phases.
- Add a 20-segment Linux kernel progress bar updated after successful 64 KiB EFI file reads, once per additional five percent. Report confirmed progress and retain failed offset diagnostics, without claiming progress during synchronous firmware calls.
- Preserve filesystem/driver ownership, security policy, boot ordering, read-only behaviour, and existing loaders. Dell Latitude 5550 direct OFS kernel boot remains unverified.


## 0.1.28 — 2026-10-21

- Respond to real Dell Latitude 5550 evidence: Boot 0.1.27 reached `OFSBOOT`, discovered the kernel, but the complete 17 MiB EFI file read failed with `EFI_DEVICE_ERROR` before the firmware memory-load retry.
- Replace the single large `EFI_FILE_PROTOCOL.Read` with bounded 64 KiB transfers into the same capped kernel-image allocation, preserving positive partial reads, explicit end-of-file and error handling, file closure, buffer ownership, and all firmware image security checks.
- Display the first failing file byte offset and occasional progress through the EFI file interface; do not add filesystem parsing or media writes to Boot.
- Automated source/build validation cannot prove full-kernel reads on the Dell SD controller. OFS kernel and initramfs boot remain unverified.


## 0.1.27 — 2026-10-21

- Resolve the next physical Dell Latitude 5550 OFS boot failure boundary: Boot 0.1.26 recognised OFSBOOT, selected `vmlinuz`, passed EFI loader-header checks and displayed `EFI_DEVICE_ERROR` during initial path-backed firmware `LoadImage`. Linux did not begin execution.
- Permit the existing Linux-only, 256 MiB-bounded memory-image retry after either firmware `EFI_NOT_FOUND` or `EFI_DEVICE_ERROR`, but only when the same EFI filesystem fully reads the candidate and its byte count equals the reported file length.
- Add immediate, human-visible diagnostics for direct filesystem open, file-info, complete-file read and memory-backed firmware load failures. Preserve firmware validation, security errors, other loaders, drivers, filesystem ownership and boot state.
- Extend the source contract and documentation. **A successful Dell Linux-from-OFS boot remains unverified** until a new physical test establishes the outcome.


## 0.1.26 — 2026-10-21

- Make clean returns from selected Linux EFI-stub images visible and require acknowledgement. Errors already pause in the existing firmware error handler; Secure Boot policy, firmware loader APIs, and the OFS driver are unchanged.
- Correct EFI loader-header allocation ownership and bound the PE-offset and gzip-signature reads after short EFI I/O.
- Remove an overwritten title allocation when creating manual boot entries.
- Dell OFSBOOT volume discovery is established with Boot 0.1.25 and OFS 0.5.48, but successful direct Linux boot and the cause of the subsequent GRUB startup are still unverified. Physical firmware qualification is outstanding.


## 0.1.25 — 2026-10-20

- Respond to physical Dell Latitude 5550 evidence: Boot 0.1.24 and OFS 0.5.46 discovered a 17 MiB Linux kernel and 86 MiB initramfs on OFSBOOT, but UEFI path-backed `LoadImage` returned `EFI_NOT_FOUND` for the selected `vmlinuz` EFI-stub loader before Linux started.
- On Linux EFI-stub entries only, retry that specific firmware lookup failure by reading the complete kernel through the already bound standard EFI filesystem protocol and calling firmware `LoadImage` with both the original device path and the memory image.
- Reject zero-length/oversized (greater than 256 MiB) images, read errors and size changes; retain normal firmware validation, secure-boot handling, memory ownership, external-driver boundaries and all other loaders.
- Add source-contract CI checks and document that loading, initramfs resolution and complete Linux boot from OFS remain unverified pending a physical Dell test. No OFS media changes or firmware boot-order changes are part of this release.


## 0.1.24 — 2026-10-20

- Remove overlapping driver-status and timing text from the graphical boot-selection menu after real Dell Latitude 5550 testing revealed that the overlay was unreadable.
- Present optional diagnostics in a dedicated, keyboard- and pointer-accessible EFI Diagnostics tool. Show concise driver and controller counts, firmware filesystem bindings, readable volume names and startup-stage timings with bounded text lengths.
- Use Boot's existing menu renderer and transient in-memory records; add no alternate filesystem code, ESP log writes, firmware-variable writes or background services.
- Extend source-contract regression checks to reject a return to main-menu diagnostic overlays. Compiled firmware operation and actual UI spacing still require physical validation.


## 0.1.23 — 2026-10-20

- Add opt-in, RAM-only filesystem-driver diagnostics to Boot's existing UEFI loading and binding path, controlled by `driver_diagnostics true` in the installed `boot.conf`. Disabled by default.
- Report begin/completion stages for EFI image validation/loading, `StartImage`, targeted controller binding, inherited DiskIo compatibility binding, and volume rescan. Distinguish firmware `ConnectController` completion from an actual newly exposed Simple File System protocol.
- Present concise driver and readable-volume counts with the last stage in Boot's graphical menu; output optional per-volume evidence to the firmware console without saving ESP logs or updating NVRAM.
- Add source-contract verification to CI and document the limits of console visibility and hardware qualification. Preserve Filesystems as the sole owner of ext4, OFS and other external driver implementations.


## 0.1.22 — 2026-10-19

- Open the configured icon directory and read leaf filenames through its EFI handle. Preserve bounded transient retries and caller handle ownership.
- Correct empty-directory icon lookup so it remains relative to the supplied base directory.
- Place all 78 runtime icons once under `icons/`, with `icons_dir icons` in the default configuration. Font and theme graphics retain their configured paths.
- Add host regression tests using the production reader and real PNG decoders. The full GNU-EFI build and package checks pass; physical boot verification of the new directory layout remains outstanding.

## 0.1.21 — 2026-10-19

- Remove 78 mirrored PNG copies while preserving the then-working direct lookup layout, artwork attribution and licences. Reduce the DEB from 172 files to 94.
- Add bundle and package checks against misplaced graphics; 0.1.22 subsequently moves the icons into their directory and rejects duplicate graphics filenames.

## 0.1.20 — 2026-10-19

- Move all firmware filesystem implementations, dormant adapters and pinned vendor source to Filesystems, preserving the code and licences.
- Build and package Boot independently of every filesystem driver, including ext4. Leave required drivers to independently installed Filesystems components.
- Remove filesystem build targets, EDK2 components and inherited installer/distribution driver-selection paths. Preserve generic UEFI driver loading and volume discovery.
- Reject any additional EFI binary or driver payload in Boot runtime bundles and Debian packages.

## 0.1.16 — 2026-10-18

- Generalise Boot's targeted driver binding from GPT/MBR logical partitions to any present BlockIo volume lacking a filesystem, including native whole-disk layouts that future Amiga RDB drivers may need.
- Keep boot discovery filesystem-agnostic: all qualified drivers in the active directory register their own EFI filesystem bindings, while Boot accesses the resulting standard SimpleFileSystem handles.
- Add a version-aware filesystem driver manifest to release packaging. It controls enabled versus disabled bundles and ensures the ZIP driver inventory exactly matches explicitly qualified release artifacts, avoiding hard-coded ext4-only packaging.
- Continue to ship read-only ext4 active for the real Linux boot path and unqualified InfiltratorFS disabled. AmigaFS is not shipped until implemented and separately qualified.
- Avoid recovery binding when an EFI driver's entry point fails to start; no GRUB, NVRAM, or installed ESP files are touched.

## 0.1.15 — 2026-10-18

- Replace the broad all-controller reconnect in the standard bundled filesystem-driver startup path with a targeted pass over present logical disk partitions that lack SimpleFileSystem.
- Avoid firmware rescans of every unrelated controller in the normal path; preserve the full connection behaviour for explicitly configured custom driver directories and interactive rescans.
- Count an EFI driver as loaded only when StartEFIImage succeeds, avoiding unnecessary follow-on volume rescans when a driver file fails to start.
- Keep ext4 discovery, filesystem scanning, graphics, OS detection and kernel handoff enabled; make no modifications to ESP installations or NVRAM.
- Add automated source-contract checks for partition-only binding, fallback behaviour and successful-driver accounting.
- This is a code-level performance candidate, not a measured claim that the approximately 17.5-second Linux loader stage is resolved. Physical timing evidence is still required before declaring a fix.

## 0.1.14 — 2026-10-18

- Fix the startup measurement checkpoints so all eight samples are recorded before the on-screen report is generated. Without this, 0.1.13's diagnostic line remained hidden even though the EFI binary compiled successfully.
- Add a CI guard requiring the sixth checkpoint. No boot discovery, loading, or storage behaviour is changed.

## 0.1.13 — 2026-10-18

- Add a RAM-only pre-menu startup timing breakdown showing first volume discovery, configuration/Secure Boot setup, EFI driver loading/connection, post-driver volume rescan, screen preparation, and loader/icon discovery.
- Show measurements as two small lines on the Boot graphical menu. Timings end before the menu countdown or any manual user delay, distinguishing startup initialization from selection wait.
- Use read-only EFI Runtime GetTime for coarse per-boot timings; no EFI variables, ESP files or NVRAM settings are written by instrumentation.
- Keep scan/driver/boot behaviour unchanged pending physical evidence; do not remove ext4 discovery merely to improve a benchmark.
- On Dell Latitude 5550 the pre-menu slowdown remains under investigation: Linux reported 17.845s and 17.542s loader periods across adjacent observations, while firmware varied from 14.139s to 11.112s. These Linux timings are not yet a breakdown of Boot itself.

## 0.1.12 — 2026-10-18

- Normalize root icon filenames when `icons_dir .` is selected; cold-start EFI filesystem drivers no longer receive a literal `.\\` path prefix.
- Retry transient EFI icon read errors at most twice (100 ms pause each), without repeatedly probing absent optional ICNS names.
- Reject incomplete successful file reads and avoid dereferencing a null image when an icon fails to decode.
- Display RAM-only icon fallback/recovery/read-error counters and EFI error names on the graphical main menu when anomalies occur, without diagnostic disk writes.
- Prevent Boot-specific previous-selection and hidden-state reads/writes from creating `EFI/BOOT/vars` when `use_nvram false` is configured on the removable test.
- A cold boot on 0.1.11 displayed striped dummy icons whereas a warm reboot on the same SD displayed normal artwork. SD hardware instability remains a suspected, unproven cause; real-firmware verification of 0.1.12 is still required.

## Unreleased

## 0.1.11 — 2026-10-18

- Restore the graphical Linux handoff: Boot now leaves GOP graphics enabled when starting a Linux EFI-stub kernel instead of switching to its text-mode loader announcement.
- For automatic Linux command lines derived from `/etc/fstab` only, add `quiet splash` to the normal entry when Plymouth is installed, allowing the installed distribution's selected boot theme to appear during direct EFI-stub boot. Explicit Linux options files remain authoritative; recovery/single-user options remain unsuppressed.
- Record that the physical Dell 800 GiB ext4 Linux Mint root successfully booted directly through Boot 0.1.10; Plymouth splash did not because the inferred command line omitted `quiet splash` and the runtime disabled graphics for Linux by default.
- Correct product banner ownership: the canonical InfiltratorOS artwork now replaces the BOOT wordmark instead of appearing beside it.
- Prevent theme generation from compositing the firmware atlas' embedded `IB` metric headers as visible font pixels; the leaked header caused dots above text and fallback glyphs.
- Validate in CI that the generated banner has no unexpected text outside the centered product artwork.

## 0.1.10 — 2026-10-17

- Add an active read-only ext4 EFI filesystem driver to the removable Boot bundle so Boot can read Linux kernels and initrds stored on ext4 instead of seeing only ESP-resident loaders such as `EFI\\ubuntu\\grubx64.efi`.
- Explicitly scan Linux kernels, fold older kernels behind the current kernel, and prefer a `vmlinuz` EFI-stub entry as the default selection while leaving GRUB discoverable as a fallback during physical qualification.
- Keep InfiltratorFS disabled for this path; the ext4 driver is the only newly active filesystem driver and its EFI write operation returns `EFI_WRITE_PROTECTED`.
- Isolate ext4 and InfiltratorFS filesystem-driver object builds so driver-specific compiler flags cannot contaminate one another through shared object filenames.
- Verify PE32+ x86-64 machine and EFI subsystem contracts for Boot, ext4 and InfiltratorFS build outputs rather than relying on filename or `file` output alone.
- Preserve the canonical MB Corpo face's actual FreeType glyph advances, side bearings and baseline geometry inside the generated firmware atlas; the runtime now consumes those embedded metrics rather than estimating spacing from glyph ink bounds.
- Record physical 0.1.9 evidence that Boot's Linux choice still chained through Ubuntu GRUB and that the first proportional-font correction improved readability without yet reproducing the canonical MB typography faithfully enough.

## 0.1.9 — 2026-10-17

- Replace the inherited pale firmware presentation with a Boot-owned theme generated from the pinned `Infiltrator-Projects/Libraries` `infiltrator-design-v1` night palette, including the canonical `#050608` background, `#2B3137` selection surface and `#00ADEF` neutral accent.
- Add a Boot product banner generated from the canonical InfiltratorOS artwork and MB Corpo A Title Cond typography instead of relying on the inherited embedded banner or presenting an unbranded empty surface.
- Replace the inherited grey selection tiles with Common-aligned focus surfaces and replace reboot, shutdown and firmware utility glyphs with one restrained Boot visual language.
- Replace inherited multicolour EFI/unknown fallback artwork with neutral Common-aligned candidate glyphs while leaving recognised operating-system identities intact.
- Hide volume badges and inherited bottom-of-screen instructional hints on the normal graphical selection surface to reduce visual noise.
- Correct MB Corpo firmware rendering: generated atlases now use the dark master expected by the renderer for correct light/dark contrast, and the runtime derives proportional glyph advances from atlas alpha bounds instead of forcing the proportional MB face into visibly artificial monospaced tracking.
- Pin theme provenance to the canonical Libraries design contract and verify palette identity, font-master contrast, generated theme geometry and required runtime assets in CI.

## 0.1.8 — 2026-10-17

- Record the first successful physical Dell Latitude 5550 Boot-to-Linux handoff: Boot starts from removable media, discovers the installed EFI paths on internal storage and successfully transfers control to Linux without requiring an OS payload on the removable card.
- Keep the separately built InfiltratorFS EFI driver under `EFI/BOOT/drivers-disabled/` in the ready-to-copy bundle instead of placing it in Boot's auto-loaded `drivers_x64` path when it is not required for ordinary OS handoff.
- Hide the inherited embedded banner by default and restrict the development-preview utility row to reboot, shutdown and firmware setup, removing inherited installer/boot-order surfaces from the normal Boot test UI.
- Place runtime menu PNGs beside `BOOTX64.EFI` and configure `icons_dir .`, following physical evidence that direct self-directory assets loaded while inherited icon-subdirectory lookups fell back to dummy graphics on the Dell firmware.
- Increase generated MB Corpo firmware typography from 18px to 30px after the physical laptop test showed the previous atlas was too small for comfortable reading.
- Extend CI so the verified bundle must contain the optional driver only in `drivers-disabled`, must contain the direct runtime InfiltratorOS icon, and must enforce the revised Boot presentation configuration.

## 0.1.7 — 2026-10-17

- Complete Boot's memory-backed EFI driver path by passing no device path to firmware `LoadImage()` when a validated driver image is already supplied in memory.
- Remove the remaining firmware filesystem/device-path dependency from InfiltratorFS driver loading while leaving OS-loader handoff unchanged.
- Follow the UEFI memory-to-memory `LoadImage()` contract so real firmware cannot re-resolve the driver file path during this load operation.

## 0.1.6 — 2026-10-17

- Load EFI drivers from Boot-owned memory buffers after Boot has opened and validated them, avoiding firmware device-path re-resolution failures observed on real Dell UEFI hardware.
- Keep OS-loader handoff path-backed so Linux, Windows and macOS loader semantics are unchanged.
- Preserve the existing `drivers_x64` runtime layout and InfiltratorFS binary contract; this release changes Boot's driver-loading path rather than the filesystem parser.

## 0.1.5 — 2026-10-17

- Generate Boot's InfiltratorOS OS icon from the canonical pinned `InfiltratorOS/assets/infiltratoros.png` artwork and verify its exact Git blob identity before conversion.
- Provide `infiltratoros`, `infiltrator` and `InfiltratorBoot` icon hints from the same product-owned artwork so discovery can identify InfiltratorOS before its root filesystem is necessarily readable.
- Ship the runtime `icons` directory Boot expects, retaining functional and third-party OS glyphs while excluding historical boot-manager product identity icons.
- Configure the runtime explicitly with `icons_dir icons`.
- Publish a deterministic ready-to-copy x86-64 UEFI bundle rooted at `EFI/BOOT`, containing `BOOTX64.EFI`, `boot.conf`, MB firmware fonts, runtime icons and the InfiltratorFS driver in `drivers_x64`.
- Include icon provenance and the generated InfiltratorOS icon in the release checksum set.

## 0.1.4 — 2026-10-17

- Generate Boot-compatible firmware bitmap atlases from the canonical pinned MB Corpo archive used by InfiltratorOS.
- Verify the archive and all three source faces against their canonical SHA-256 identities before generation.
- Publish MB Corpo S Regular, MB Corpo S Bold and MB Corpo A Title Cond Regular firmware atlases with the Boot release.
- Configure Boot to use MB Corpo S Regular for its current single-font firmware UI.
- Include font provenance and all generated font assets in the release checksum manifest.

## 0.1.3 — 2026-10-17

- Rename Boot's runtime configuration contract from the inherited `refind.conf` name to `boot.conf`.
- Publish `boot.conf` as a verified release asset beside the EFI binaries.
- Keep removable-media qualification state off motherboard NVRAM by default while physical testing is in progress.

## 0.1.2 — 2026-10-17

- Publish the exact GNU-EFI x86-64 Boot application produced by CI as a versioned EFI artifact.
- Publish the exact read-only InfiltratorFS EFI driver produced by the same build.
- Seal both EFI binaries, the tracked source archive and source-commit identity in one SHA-256 manifest and verify the complete remote release before and after publication.
- Keep firmware-runtime, Secure Boot, persistent-installation and recovery qualification explicitly separate; the release performs no installation and does not modify ESP or NVRAM state.

## Source preview 0.1.0 — 2026-10-16

- Qualify the complete GNU-EFI x86-64 program alongside the read-only InfiltratorFS driver.
- Replace host-dependent default toolchain selection with explicit GNU-EFI defaults and preserve recursive make flags.
- Keep generic cleanup inside repository-owned outputs.
- Publish exact source as an immutable prerelease, with binary/runtime qualification explicitly separate.

All notable Boot project changes are recorded here.

This changelog tracks the Infiltrator project itself. Historical upstream change history imported with the initial source tree remains in `NEWS.txt` and is not duplicated here.

## Unreleased

### Changed

- Renamed the component to Boot across maintained documentation and integration metadata.
- Archived imported package definitions as source provenance so the current product cannot publish under the historical upstream package identity.
- Recorded InfiltratorOS's shared graphical product contract and emergency-only terminal policy.
- Reframed the repository around Boot as the product authority rather than its historical source basis.
- Added project-specific engineering, design, architecture, product-quality, safety, validation and provenance documentation under the canonical Infiltrator Engineering Philosophy.
- Added maintained build and contribution guidance.
- Defined inherited names and source structure as transitional implementation details rather than product identity.
- Reworked vendored InfiltratorFS maintenance so generation is explicit and CI verifies committed source instead of authoring and pushing source changes automatically.
- Tightened InfiltratorFS UEFI CI around explicit qualification semantics and immutable action identity.
