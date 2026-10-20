#!/usr/bin/env python3
"""Source-contract regression for Linux EFI-stub loading from external FS.

The x86-64 EFI build validates compilation. On-device EFI LoadImage behaviour
and the subsequent kernel/initramfs handoff still require physical testing.
"""
from pathlib import Path

source = Path("refind/launch_efi.c").read_text()
start = source.index("static EFI_STATUS LoadLinuxKernelImage(")
end = source.index("// Launch an EFI binary.", start)
fallback = source[start:end]
launcher = source[end:source.index("} /* EFI_STATUS StartEFIImage() */", end)]

assert "#define BOOT_MAX_KERNEL_SOURCE_BYTES (256U * 1024U * 1024U)" in source
assert "DevicePath, NULL, 0, ImageHandle" in fallback
assert "Status != EFI_NOT_FOUND || RootDir == NULL || DevicePath == NULL" in fallback
assert "EFI_FILE_MODE_READ, 0" in fallback
assert "Info = LibFileInfo(FileHandle);" in fallback
assert "ExpectedSize == 0 || ExpectedSize > BOOT_MAX_KERNEL_SOURCE_BYTES" in fallback
assert "Status = egLoadFile(RootDir, Filename, ImageData, &ImageSize)" in fallback
assert "(UINT64)ImageSize != ExpectedSize" in fallback
assert "DevicePath, *ImageData, ImageSize, ImageHandle" in fallback
assert "SecureBoot" not in fallback and "SetVariable" not in fallback

# The normal path is still first and is not retried for security or format
# errors. Other EFI loaders and drivers retain their prior loading modes.
assert "else if (OSType == 'L')" in launcher
assert "LoadLinuxKernelImage(Volume->RootDir, Filename, DevicePath" in launcher
assert "BS->LoadImage, FALSE, SelfImageHandle, NULL," in launcher
assert "BS->LoadImage, FALSE, SelfImageHandle, DevicePath," in launcher
assert "Status == EFI_SECURITY_VIOLATION" in launcher
assert "WarnSecureBootError" in launcher
assert "MyFreePool(ImageData);" in launcher

print("Linux EFI-stub external-filesystem LoadImage fallback contract passed")
