# Compiles BOOT's firmware entry and menu/loader modules as an EFI image.
# Its output is a build artifact; the native installer separately owns
# source pinning, ESP placement, and firmware entry registration.

SRCDIR = .

VPATH = $(SRCDIR)

ARCH            = $(shell uname -m | sed s,i[3456789]86,ia32,)

TARGET	= boot.efi

ifeq ($(ARCH),ia32)
  LIBEG = build32
  TARGET = boot_ia32.efi
endif

ifeq ($(ARCH),x86_64)
  LIBEG = build64
  TARGET = boot_x64.efi
endif

ifeq ($(ARCH),aarch64)
  LIBEG = build
  TARGET = boot_aa64.efi
endif

LOCAL_GNUEFI_CFLAGS  = -I$(SRCDIR) -I$(SRCDIR)/../include \
                       -I$(SRCDIR)/../libeg -I$(SRCDIR)/../mok
LOCAL_LDFLAGS   = -L$(SRCDIR)/../libeg/ -L$(SRCDIR)/../mok/ \
                  -L$(SRCDIR)/../EfiLib/ -L$(SRCDIR)/../gzip
LOCAL_LIBS      = -leg -lmok -lEfiLib -lgzip

OBJS            = apple.o config.o crc32.o driver_support.o gpt.o icns.o \
                  install.o launch_efi.o launch_legacy.o lib.o line_edit.o \
                  linux.o log.o lvm2.o main.o menu.o mystrings.o pointer.o scan.o \
                  screen.o theme.o

include $(SRCDIR)/../Make.common

# LVM creates callable EFI Block I/O and Disk I/O protocols in the BOOT
# application image. Unlike ordinary BOOT application functions, those
# callbacks are invoked from separately loaded EFI filesystem drivers.
# GNU-EFI's EFI_FUNCTION_WRAPPER routes calls into firmware but does not
# guarantee that EFIAPI callbacks themselves are compiled with the Microsoft
# x64 ABI. Filesystem drivers already force this flag in Make.gnuefi.
# Keep the ABI override limited to the LVM protocol implementation rather
# than altering BOOT's unrelated menu, firmware and loader interfaces.
ifeq ($(MAKEWITH),GNUEFI)
ifeq ($(ARCH),x86_64)
lvm2.o: LOCAL_GNUEFI_CFLAGS += "-DEFIAPI=__attribute__((ms_abi))"
endif
endif

all: $(TARGET)
# Resolve BOOT's references against the firmware support archives at the ELF
# link. The objcopy rule below carries the loaded data and relocation sections
# forward into the executable firmware image.
$(SHLIB_TARGET): $(OBJS)
	$(LD) $(LOCAL_LDFLAGS) $(GNUEFI_LDFLAGS) $(SUBSYSTEM_LDFLAG) $(OBJS) \
	      -o $@ $(LOCAL_LIBS) $(GNUEFI_LIBS)

$(TARGET): $(SHLIB_TARGET)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rodata \
		   -j .rel -j .rela -j .rel.* -j .rela.* -j .rel* -j .rela* \
		   -j .reloc --strip-unneeded $(FORMAT) $< $@
ifneq ($(OMIT_SBAT), 1)
	    $(OBJCOPY) --add-section .sbat=$(SRCDIR)/../$(BOOT_SBAT_CSV) \
		       --adjust-section-vma .sbat+10000000 $@
endif
	chmod a-x $(TARGET)

clean:
	rm -f $(TARGET) *~ *.so $(OBJS) *.efi *.obj boot_*.txt \
		boot_*.dll *.lib
