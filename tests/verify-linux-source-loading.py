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
assert "(Status != EFI_NOT_FOUND && Status != EFI_DEVICE_ERROR)" in fallback
assert "RootDir == NULL || DevicePath == NULL" in fallback
assert "Status = refit_call5_wrapper(RootDir->Open" in fallback
assert "Kernel EFI file read failed at byte %llu of %llu: %r" in fallback
assert "#define BOOT_KERNEL_PROGRESS_SEGMENTS 20U" in source
assert "Kernel phase 1/4: firmware device-path load" in fallback
assert "Kernel phase 2/4: EFI filesystem fallback" in fallback
assert "Kernel phase 3/4: read %llu bytes" in fallback
assert "Kernel phase 4/4: starting Linux EFI image" in launcher
assert "PrintLinuxKernelReadProgress(0U, ExpectedSize)" in fallback
assert "if (NextSegment > ProgressSegment)" in fallback
assert fallback.index("ImageSize += ReadSize;") < fallback.index("PrintLinuxKernelReadProgress(ImageSize, ExpectedSize);")
assert "if (Verbose && !IsDriver && OSType == 'L')" in launcher
assert "Memory-backed kernel LoadImage failed: %r" in fallback
assert "EFI_FILE_MODE_READ, 0" in fallback
assert "Info = LibFileInfo(FileHandle);" in fallback
assert "ExpectedSize == 0 || ExpectedSize > BOOT_MAX_KERNEL_SOURCE_BYTES" in fallback
assert "*ImageData = AllocatePool((UINTN)ExpectedSize)" in fallback
assert "while (ImageSize < (UINTN)ExpectedSize)" in fallback
assert "RequestedSize > 64U * 1024U" in fallback
assert "FileHandle->Read, FileHandle, &ReadSize," in fallback
assert "ReadSize == 0 || ReadSize > RequestedSize" in fallback
assert "ImageSize += ReadSize;" in fallback
assert "MyFreePool(*ImageData);" in fallback
assert "*ImageData = NULL;" in fallback
assert "CloseStatus = refit_call1_wrapper(FileHandle->Close, FileHandle);" in fallback
assert "DevicePath, *ImageData, ImageSize, ImageHandle" in fallback
assert "SecureBoot" not in fallback and "SetVariable" not in fallback

# Only path-loading NOT_FOUND and DEVICE_ERROR reach the independent full-read
# gate. Security, invalid-image and resource errors never trigger a retry.
# Other EFI loaders and drivers retain their prior loading modes.
assert "else if (OSType == 'L')" in launcher
assert "LoadLinuxKernelImage(Volume->RootDir, Filename, DevicePath" in launcher
assert "BS->LoadImage, FALSE, SelfImageHandle, NULL," in launcher
assert "BS->LoadImage, FALSE, SelfImageHandle, DevicePath," in launcher
assert "Status == EFI_SECURITY_VIOLATION" in launcher
assert "WarnSecureBootError" in launcher
assert "MyFreePool(ImageData);" in launcher

print("Linux EFI-stub external-filesystem LoadImage fallback contract passed")

# EFI header validation and handoff must fail visibly and free owned buffers.
header = source.split('UINTN IsValidLoader(', 1)[1].split('} // UINTN IsValidLoader()', 1)[0]
loader = source.split('VOID StartLoader(', 1)[1].split('} // VOID StartLoader()', 1)[0]
assert header.index('if ((RootDir == NULL) || (FileName == NULL))') < header.index('Header = AllocateZeroPool')
assert 'MyFreePool(Header);\n        return LOADER_TYPE_INVALID;' in header
assert 'if (LoadedSize >= 0x40)' in header
assert 'MyCopyMem(&SignaturePos, &Header[0x3c], sizeof(SignaturePos));' in header
assert 'LoadedSize >= 2' in header
assert 'Status = StartEFIImage(' in loader
assert "Entry->OSType == 'L' && !EFI_ERROR(Status)" in loader
assert 'PauseForKey();' in loader
manual = Path('refind/config.c').read_text().split('static LOADER_ENTRY * AddStanzaEntries(', 1)[1].split('} // static VOID AddStanzaEntries()', 1)[0]
assert 'Entry->me.Title        = AllocateZeroPool' not in manual
