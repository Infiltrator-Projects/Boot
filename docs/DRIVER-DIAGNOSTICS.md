# EFI filesystem-driver diagnostics

Boot owns driver discovery and firmware controller connection. Filesystems owns
independently built EFI drivers. This diagnostic mode inspects Boot's real
UEFI Driver Binding workflow and does not embed or emulate OFS, ext4 or any
other filesystem parser.

## Enabling a controlled physical test

On the **existing Boot installation**, add this single line to the copy of
`boot.conf` beside the executable selected by firmware:

```text
driver_diagnostics true
```

The default is **off**. Keep `log_level 0` (the default) and
`use_nvram false` when testing. Infiltrator Boot does not need to write the
ESP or firmware variables to collect these diagnostics. Using the regular
`log_level` facility instead would write and rotate files and is not a
substitute for a read-only test.

Run the test only by a one-time boot selection, with a verified GRUB recovery
entry. Do not replace the known-good Boot executable or activate unqualified
drivers to enable diagnostics.

## What the mode reports

As Boot executes its normal driver-loading path, the firmware console receives
stage markers immediately before and after EFI image validation,
`LoadImage`, `StartImage`, inherited DiskIo compatibility binding,
targeted `ConnectController` and the volume rescan. A successful
`ConnectController` call is **not** interpreted as successful mounting: the
diagnostic separately checks whether `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` became
available on that controller.

After the rescan, Boot reports up to 32 readable volumes by firmware-discovered
name and filesystem label on the console. The graphical main menu displays
two short lines containing the count of started external drivers, attempted
controller connections, newly available SimpleFileSystem protocols, readable
volumes and the last recorded stage.

The graphical summary is **volatile**; it appears only if Boot reaches the
menu. A marker before a blocked UEFI call may appear on the console, but
some firmware hides pre-graphics console output or does not flush it. There is
**no guaranteed diagnostic persistence across a hang or reset**. Capture a
photograph of the visible stage if testing a firmware that stalls.

This mode cannot promise a firmware I/O timeout, identify which filesystem
driver supported a controller if more than one is present, or detect why a
driver blocked internally without that driver's own opt-in instrumentation.
Controller counts represent handles; they are not unique physical disks.
Readable volumes include volumes already provided by firmware before external
drivers ran.

## Failure and recovery

The diagnostic itself does not change device binding order, repeat controller
connections, introduce new filesystem algorithms, write to NVRAM or save an
ESP log. The inherited compatibility connection/disconnection behaviour
remains unchanged and is identified at its call boundaries.

If the firmware stalls, use the existing firmware recovery mechanism (for the
Dell Latitude 5550, the F12 one-time boot menu and Ubuntu/GRUB entry). Disable
the experimental driver before reattempting Boot. Restore the original
`boot.conf` setting after collecting results.

Qualification: the GNU-EFI build and source checks can validate compilation
and instrumentation placement but **cannot** prove console visibility or
hardware behaviour. Real-device verification is required separately.
