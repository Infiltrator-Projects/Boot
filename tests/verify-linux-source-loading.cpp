// SPDX-License-Identifier: GPL-3.0-or-later
// Native C++17 source contract for Linux EFI-stub loading via independent FS.
// Static verification is not a substitute for hardware EFI handoff testing.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
static void check(bool pass,const std::string& why) {
    if(!pass)throw std::runtime_error(why);
}
static std::string read(const fs::path& path) {
    std::ifstream file(path,std::ios::binary);
    check(bool(file),"Cannot read Boot source: "+path.string());
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
static std::size_t at(const std::string& data,const std::string& token) {
    const auto pos=data.find(token);
    check(pos!=std::string::npos,"EFI source invariant missing: "+token);
    return pos;
}
static void absent(const std::string& data,const std::string& token) {
    check(data.find(token)==std::string::npos,"Forbidden EFI source token: "+token);
}
static std::string section(const std::string& source,const std::string& start,
                           const std::string& finish){
    const auto begin=at(source,start);
    const auto end=source.find(finish,begin+start.size());
    check(end!=std::string::npos,"Cannot find EFI source section ending: "+finish);
    return source.substr(begin,end-begin);
}
int main(int argc,char** argv) {
    try{
        check(argc<=2,"Usage: verify-linux-source-loading [BOOT_SOURCE_ROOT]");
        const fs::path root=argc==2?fs::path(argv[1]):fs::path(".");
        const auto source=read(root/"refind/launch_efi.c");
        const auto fallback=section(source,"static EFI_STATUS LoadLinuxKernelImage(",
                                    "// Launch an EFI binary.");
        const auto launcher=section(source,"// Launch an EFI binary.",
                                    "} /* EFI_STATUS StartEFIImage() */");
        const auto config=read(root/"refind/config.c");
        const auto scan=read(root/"refind/scan.c");
        const auto header=read(root/"refind/launch_efi.h");
        const auto drivers=read(root/"refind/driver_support.c");
        for(const char* token:{
            "#define BOOT_MAX_KERNEL_SOURCE_BYTES (256U * 1024U * 1024U)",
            "#define BOOT_KERNEL_PROGRESS_SEGMENTS 20U",
            "#define BOOT_KERNEL_PROGRESS_BYTES (256U * 1024U)",
            "IN BOOLEAN MemoryFirst,",
            "Entry->LinuxMemoryFirst",
            "elapsed %us",
            "| %us | %lluKiB/s | ETA %llus",
            "BootKernelClockSeconds(",
            "BootKernelElapsedSeconds("
        })at(source,token);
        for(const char* token:{
            "DevicePath, NULL, 0, ImageHandle",
            "(Status != EFI_NOT_FOUND && Status != EFI_DEVICE_ERROR)",
            "RootDir == NULL || DevicePath == NULL",
            "Status = refit_call5_wrapper(RootDir->Open",
            "Kernel EFI file read failed at byte %llu of %llu: %r",
            "BootKernelElapsedSeconds(ReadStarted, &Elapsed)",
            "Kernel EFI read: %llu confirmed KiB in %u s before failure",
            "Kernel phase 1/4: firmware device-path load",
            "Kernel phase 1/4: memory-first; skipping firmware path load",
            "if (!MemoryFirst)",
            "Kernel phase 2/4: EFI filesystem read",
            "Kernel phase 3/4: read %llu bytes",
            "HaveReadStart = BootKernelClockSeconds(&ReadStarted)",
            "PrintLinuxKernelReadProgress(0U, ExpectedSize, HaveReadStart, ReadStarted)",
            "PrintLinuxKernelReadProgress(ImageSize, ExpectedSize, HaveReadStart, ReadStarted)",
            "if (ImageSize == ReadSize || NextStep > ProgressStep ||",
            "first EFI file read beginning at byte 0",
            "Memory-backed kernel LoadImage failed: %r",
            "EFI_FILE_MODE_READ, 0",
            "Info = LibFileInfo(FileHandle);",
            "ExpectedSize == 0 || ExpectedSize > BOOT_MAX_KERNEL_SOURCE_BYTES",
            "*ImageData = AllocatePool((UINTN)ExpectedSize)",
            "while (ImageSize < (UINTN)ExpectedSize)",
            "RequestedSize > 64U * 1024U",
            "FileHandle->Read, FileHandle, &ReadSize,",
            "ReadSize == 0 || ReadSize > RequestedSize",
            "ImageSize += ReadSize;",
            "MyFreePool(*ImageData);",
            "*ImageData = NULL;",
            "CloseStatus = refit_call1_wrapper(FileHandle->Close, FileHandle);",
            "DevicePath, *ImageData, ImageSize, ImageHandle"
        })at(fallback,token);
        check(at(fallback,"ImageSize += ReadSize;")<
              at(fallback,"PrintLinuxKernelReadProgress(ImageSize, ExpectedSize, HaveReadStart, ReadStarted);"),
              "EFI source must count confirmed bytes before reporting progress");
        absent(fallback,"SecureBoot");
        absent(fallback,"SetVariable");
        for(const char* token:{
            "Verbose, LinuxMemoryFirst,",
            "Kernel phase 4/4: starting Linux EFI image",
            "if (Verbose && !IsDriver && OSType == 'L')",
            "else if (OSType == 'L')",
            "LoadLinuxKernelImage(Volume->RootDir, Filename, DevicePath",
            "BS->LoadImage, FALSE, SelfImageHandle, NULL,",
            "BS->LoadImage, FALSE, SelfImageHandle, DevicePath,",
            "Status == EFI_SECURITY_VIOLATION",
            "WarnSecureBootError",
            "MyFreePool(ImageData);"
        })at(launcher,token);
        for(const char* token:{
            "MyStriCmp(TokenList[0], L\"linux_load\")",
            "Entry->LinuxMemoryFirst && Entry->OSType != 'L'"
        })at(config,token);
        at(scan,"NewEntry->LinuxMemoryFirst = Entry->LinuxMemoryFirst");
        at(header,"IN BOOLEAN LinuxMemoryFirst,");
        at(header,"IN BOOLEAN StorageDriver);");
        at(drivers,"FALSE, TRUE, FALSE, StorageProvider);");
        const auto valid=section(source,"UINTN IsValidLoader(",
                                  "} // UINTN IsValidLoader()");
        const auto loader=section(source,"VOID StartLoader(",
                                   "} // VOID StartLoader()");
        check(at(valid,"if ((RootDir == NULL) || (FileName == NULL))")<
              at(valid,"Header = AllocateZeroPool"),
              "EFI header arguments must be checked before allocation");
        for(const char* token:{
            "MyFreePool(Header);\n        return LOADER_TYPE_INVALID;",
            "if (LoadedSize >= 0x40)",
            "MyCopyMem(&SignaturePos, &Header[0x3c], sizeof(SignaturePos));",
            "LoadedSize >= 2"
        })at(valid,token);
        for(const char* token:{
            "Status = StartEFIImage(",
            "Entry->OSType == 'L' && !EFI_ERROR(Status)",
            "PauseForKey();"
        })at(loader,token);
        const auto manual=section(config,"static LOADER_ENTRY * AddStanzaEntries(",
                                   "} // static VOID AddStanzaEntries()");
        absent(manual,"Entry->me.Title        = AllocateZeroPool");
        check(!fs::exists(root/"tests/verify-linux-source-loading.py"),
              "Retired Python loading regression still exists");
        std::cout<<"Linux EFI-stub external-filesystem fallback contract passed (native C++17)\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"Boot Linux external-filesystem source contract failed: "<<error.what()<<'\n';
        return 1;
    }
}
