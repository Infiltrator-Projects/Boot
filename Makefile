# Makefile for rEFInd

# This program is licensed under the terms of the GNU GPL, version 3,
# or (at your option) any later version.
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

include Make.common

SHELL=/bin/bash
LOADER_DIR=refind
LIBEG_DIR=libeg
MOK_DIR=mok
GZIP_DIR=gzip
GPTSYNC_DIR=gptsync
EFILIB_DIR=EfiLib
# Two possible locations for TianoCore toolkit:
# TIANOBASE is used with "tiano" targets and
# EDK2BASE is used with "edk2" targets
export TIANOBASE=/usr/local/UDK2014/MyWorkSpace
export EDK2BASE=/usr/local/edk2-vUDK2018
# NOTE: Below is overridden for "tiano" targets
-include $(EDK2BASE)/Conf/target.txt
THISDIR=$(shell pwd)
EDK2_BUILDLOC=$(EDK2BASE)/Build/Refind/$(TARGET)_$(TOOL_CHAIN_TAG)/$(UC_ARCH)
EDK2_PROGRAM_BASENAMES=refind gptsync
EDK2_PROGRAMS=$(EDK2_PROGRAM_BASENAMES:=.efi)
EDK2_ALL_BASENAMES=$(EDK2_PROGRAM_BASENAMES)
EDK2_ALL_FILES=$(EDK2_ALL_BASENAMES:=.efi)

# GNU-EFI is the deterministic default. Other toolchains require explicit targets.
.PHONY: all gptsync gnuefi all_gnuefi gptsync_gnuefi tiano all_tiano gptsync_tiano edk2 all_edk2 gptsync_edk2 build_edk2 clean install
all: gnuefi
gptsync: gptsync_gnuefi

###########################################################################
#
# GNU-EFI build rules; should work with any CPU architecture and GNU-EFI
# version 3.0u or later, but cross-compiling requires adding odd
# components and is untested....
#
###########################################################################

gnuefi:
	+$(MAKE) MAKEWITH=GNUEFI -C $(LIBEG_DIR)
	+$(MAKE) MAKEWITH=GNUEFI -C $(MOK_DIR)
	+$(MAKE) MAKEWITH=GNUEFI -C $(GZIP_DIR)
	+$(MAKE) MAKEWITH=GNUEFI -C $(EFILIB_DIR)
	+$(MAKE) MAKEWITH=GNUEFI -C $(LOADER_DIR)
	+$(MAKE) MAKEWITH=GNUEFI -C $(GPTSYNC_DIR) gnuefi

all_gnuefi: gnuefi

gptsync_gnuefi:
	+$(MAKE) MAKEWITH=GNUEFI -C $(GPTSYNC_DIR) gnuefi

###########################################################################
#
# Old-style TianoCore build rules; useful only with UDK2014 development
# kit....
#
###########################################################################

# Don't build gptsync under TianoCore/AARCH64 by default because it errors out
# when using a cross-compiler on an x86-64 system. Because gptsync is pretty
# useless on ARM64, skipping it is no big deal....
tiano:
	-include $(TIANOBASE)/Conf/target.txt
	+$(MAKE) MAKEWITH=TIANO AR_TARGET=EfiLib -C $(EFILIB_DIR) -f Make.tiano
	+$(MAKE) MAKEWITH=TIANO AR_TARGET=libeg -C $(LIBEG_DIR) -f Make.tiano
	+$(MAKE) MAKEWITH=TIANO AR_TARGET=mok -C $(MOK_DIR) -f Make.tiano
	+$(MAKE) MAKEWITH=TIANO AR_TARGET=gzip -C $(GZIP_DIR) -f Make.tiano
	+$(MAKE) MAKEWITH=TIANO BUILDME=refind DLL_TARGET=refind -C $(LOADER_DIR) -f Make.tiano
ifneq ($(ARCH),aarch64)
	+$(MAKE) MAKEWITH=TIANO -C $(GPTSYNC_DIR) -f Make.tiano
endif

all_tiano: tiano

gptsync_tiano:
	-include $(TIANOBASE)/Conf/target.txt
	+$(MAKE) MAKEWITH=TIANO -C $(GPTSYNC_DIR) -f Make.tiano

###########################################################################
#
# New-style build rules for use with TianoCore via its own "build" tool.
# Works with UDK2014 and late-March EDK2; but UDK2014 builds fail with
# AARCH64 unless .inf files are modified to eliminate references to
# CompilerIntrinsicsLib....
#
# These build the firmware application and gptsync, even if only one
# component is requested;
# only the requested components are copied to their final destinations.
# This is done because of the heavy overhead in building just one
# component in the TianoCore "build" tool.
#
###########################################################################

# Build process for TianoCore using TianoCore-standard build process rather
# than my own custom Makefiles (except this top-level one)
edk2: build_edk2
	cp $(EDK2_BUILDLOC)/refind.efi ./refind/refind_$(FILENAME_CODE).efi
	cp $(EDK2_BUILDLOC)/gptsync.efi ./gptsync/gptsync_$(FILENAME_CODE).efi
ifneq ($(OMIT_SBAT), 1)
	$(OBJCOPY) --set-section-alignment '.sbat=512' --add-section .sbat=$(REFIND_SBAT_CSV) \
		--adjust-section-vma .sbat+10000000 ./refind/refind_$(FILENAME_CODE).efi
	$(OBJCOPY) --set-section-alignment '.sbat=512' --add-section .sbat=$(REFIND_SBAT_CSV) \
		--adjust-section-vma .sbat+10000000 ./gptsync/gptsync_$(FILENAME_CODE).efi
endif

all_edk2: build_edk2
	cp $(EDK2_BUILDLOC)/refind.efi ./refind/refind_$(FILENAME_CODE).efi
	cp $(EDK2_BUILDLOC)/gptsync.efi ./gptsync/gptsync_$(FILENAME_CODE).efi

gptsync_edk2: build_edk2
	cp $(EDK2_BUILDLOC)/gptsync.efi ./gptsync/gptsync_$(FILENAME_CODE).efi


build_edk2: $(EDK2BASE)/RefindPkg
	cd $(EDK2BASE) && \
	. ./edksetup.sh BaseTools && \
	build -a $(UC_ARCH) -p RefindPkg/RefindPkg.dsc

$(EDK2BASE)/RefindPkg:
	ln -s $(THISDIR) $(EDK2BASE)/RefindPkg

###########################################################################
#
# Build rules that are not dependent on the toolkit....
#
###########################################################################

# NOTE: This "clean" rule cleans intermediate components for all three
# build styles (tiano, edk2, and gnuefi).
clean:
	$(MAKE) -C $(LIBEG_DIR) clean
	$(MAKE) -C $(MOK_DIR) clean
	$(MAKE) -C $(GZIP_DIR) clean
	$(MAKE) -C $(LOADER_DIR) clean
	$(MAKE) -C $(EFILIB_DIR) clean
	$(MAKE) -C $(GPTSYNC_DIR) clean
	rm -f include/*~

# NOTE TO DISTRIBUTION MAINTAINERS:
# The "install" target installs the program directly to the ESP
# and it modifies the *CURRENT COMPUTER's* NVRAM. Thus, you should
# *NOT* use this target as part of the build process for your
# binary packages (RPMs, Debian packages, etc.). (Gentoo could
# use it in an ebuild, though....) You COULD, however, copy the
# files to a directory somewhere (/usr/share/refind or whatever)
# and then call refind-install as part of the binary package
# installation process.

install:
	./refind-install

# DO NOT DELETE
