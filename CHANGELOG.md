# Changelog

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
