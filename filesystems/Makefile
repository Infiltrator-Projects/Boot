# Selects filesystem driver build targets. The normal GNU-EFI release
# build requires ext2, ext4, reiserfs, iso9660, hfs and btrfs.
# Installation is intentionally excluded from this driver build file.
# BOOT's native .run and generic .deb alone own the fixed ESP destination
# /EFI/InfiltratorOS/BOOT and associated firmware registration.

FILESYSTEMS = ext2 ext4 reiserfs iso9660 hfs btrfs
FILESYSTEMS_GNUEFI = ext2_gnuefi ext4_gnuefi reiserfs_gnuefi iso9660_gnuefi hfs_gnuefi btrfs_gnuefi
TEXTFILES = $(FILESYSTEMS:=*.txt)

all:	$(FILESYSTEMS)

ext2:
	rm -f fsw_efi.obj
	+make DRIVERNAME=ext2 -f Make.tiano

ext4:
	rm -f fsw_efi.obj
	+make DRIVERNAME=ext4 -f Make.tiano

reiserfs:
	rm -f fsw_efi.obj
	+make DRIVERNAME=reiserfs -f Make.tiano

iso9660:
	rm -f fsw_efi.obj
	+make DRIVERNAME=iso9660 -f Make.tiano

hfs:
	rm -f fsw_efi.obj
	+make DRIVERNAME=hfs -f Make.tiano

btrfs:
	rm -f fsw_efi.obj
	+make DRIVERNAME=btrfs -f Make.tiano

ntfs:
	rm -f fsw_efi.obj
	+make DRIVERNAME=ntfs -f Make.tiano

gnuefi: $(FILESYSTEMS_GNUEFI)

all_gnuefi:	$(FILESYSTEMS_GNUEFI)

ext2_gnuefi:
	rm -f fsw_efi.o
	+make DRIVERNAME=ext2 -f Make.gnuefi

ext4_gnuefi:
	rm -f fsw_efi.o
	+make DRIVERNAME=ext4 -f Make.gnuefi

reiserfs_gnuefi:
	rm -f fsw_efi.o
	+make DRIVERNAME=reiserfs -f Make.gnuefi

iso9660_gnuefi:
	rm -f fsw_efi.o
	+make DRIVERNAME=iso9660 -f Make.gnuefi

hfs_gnuefi:
	rm -f fsw_efi.o
	+make DRIVERNAME=hfs -f Make.gnuefi

btrfs_gnuefi:
	rm -f fsw_efi.o
	+make DRIVERNAME=btrfs -f Make.gnuefi

ntfs_gnuefi:
	rm -f fsw_efi.o
	+make DRIVERNAME=ntfs -f Make.gnuefi
# All backends share the same fsw_efi.o pathname. Each recipe removes and
# rebuilds it with its own FSTYPE. Serialising the backend targets prevents
# one driver's protocol table from being linked into another image.
.NOTPARALLEL:

clean:
	rm -f *~ *.bak *.o *.obj *.so *.efi *.dll err.txt $(TEXTFILES)
	+make -C test clean

