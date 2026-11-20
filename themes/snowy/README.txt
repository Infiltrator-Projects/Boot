This is the Snowy theme for BOOT (https://github.com/Infiltrator-Projects/Boot). To
use it:

1) Unpack the .zip file (which you've presumably already done).

2) Copy the "snowy" subdirectory to your BOOT installation. BOOT can be
   located various places, depending on your OS:
   * On most Linux systems, the ESP is mounted at /boot/efi, so BOOT is
     at /boot/efi/EFI/InfiltratorBoot.
   * On OS X, if you haven't configured the ESP to auto-mount, you should
     run the "mountesp" script that comes with BOOT 0.10.0 and later.
     This will mount the ESP, typically at /Volumes/ESP, so BOOT will
     probably be at /Volumes/ESP/EFI/InfiltratorBoot.
   * On Windows, the ESP is not mounted by default. In an Administrator
     Command Prompt window, you can type "mountvol S: /S" to mount the ESP
     as S:. (You can change the device letter, if necessary.) BOOT should
     then normally be S:\EFI\boot.

3) Open the boot.conf file in the BOOT directory in your favorite
   text editor.

4) Add the following line to the END of your boot.conf file:
   include boot-snowy.conf

You can swap out any element, including the banner/background image, as you
see fit. The banner-snowy.png image is 1920x1080. If your display is
another size, the image will be scaled to fit. If you see artifacts, or if
your display does not use a 16:9 aspect ratio, you may want to load the
file into a graphics editor to scale it or to crop it.

This theme is derived from icons from several sources; see snowy/README for
details.
