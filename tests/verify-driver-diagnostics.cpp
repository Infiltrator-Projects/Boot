// SPDX-License-Identifier: GPL-3.0-or-later
// RAM-only UEFI driver diagnostics source contract. Static source evidence
// complements, but never replaces, real EFI build and hardware testing.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
static void demand(bool yes,const std::string& reason){
    if(!yes)throw std::runtime_error(reason);
}
static std::string read(const fs::path& path){
    std::ifstream stream(path,std::ios::binary);
    demand(bool(stream),"Missing Boot source file: "+path.string());
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
static void must(const std::string& data,const std::string& token){
    demand(data.find(token)!=std::string::npos,"Boot diagnostics contract lost: "+token);
}
static void must_not(const std::string& data,const std::string& token){
    demand(data.find(token)==std::string::npos,"Boot diagnostics forbidden token returned: "+token);
}
static std::string segment(const std::string& text,const std::string& start,
                           const std::string& finish) {
    const auto a=text.find(start);
    demand(a!=std::string::npos,"Boot source segment start absent: "+start);
    const auto b=text.find(finish,a+start.size());
    demand(b!=std::string::npos,"Boot source segment end absent: "+finish);
    return text.substr(a,b-a);
}
int main(int argc,char**argv){
    try{
        demand(argc<=2,"Usage: verify-driver-diagnostics [BOOT_REPOSITORY_ROOT]");
        const fs::path root=argc==2?argv[1]:".";
        const auto main=read(root/"refind/main.c");
        const auto global=read(root/"refind/global.h");
        const auto config=read(root/"refind/config.c");
        const auto drivers=read(root/"refind/driver_support.c");
        const auto launch=read(root/"refind/launch_efi.c");
        const auto menu=read(root/"refind/menu.c");
        const auto defaults=read(root/"boot.conf");
        const auto scan=read(root/"refind/scan.c");
        must(global,"BOOLEAN          DriverDiagnostics;");
        must(main,"/* DriverDiagnostics = */ FALSE");
        must(config,"L\"driver_diagnostics\"");
        must(defaults,"# driver_diagnostics true");
        must_not(defaults,"\ndriver_diagnostics true\n");
        for(const char* action:{"verify EFI image","LoadImage","StartImage","compatibility binding"})
            must(launch,std::string("L\"")+action+"\"");
        for(const char* action:{"ConnectController","compat DisconnectController",
                                "compat ConnectController","volume rescan"})
            must(drivers+main,std::string("L\"")+action+"\"");
        must(launch,"BootDriverDiagnosticProgress(L\"StartImage\", ImageTitle, FALSE");
        must(launch,"BootDriverDiagnosticProgress(L\"StartImage\", ImageTitle, TRUE");
        must(drivers,"BootDriverDiagnosticProgress(L\"ConnectController\", L\"BlockIo handle\", FALSE");
        must(drivers,"BootDriverDiagnosticProgress(L\"ConnectController\", L\"BlockIo handle\", TRUE");
        must(drivers,"DiagNewFilesystems");
        must(drivers,"&gMyEfiSimpleFileSystemProtocolGuid");
        must(main,"BootDriverDiagnosticVolumes()");
        must(menu,"BootGetDriverDiagnosticLine(Line)");
        must(menu,"GlobalConfig.DriverDiagnostics");
        must(global,"TAG_DRIVER_DIAGNOSTICS");
        must(main,"case TAG_DRIVER_DIAGNOSTICS:");
        must(main,"ShowBootDiagnostics();");
        must(menu,"VOID ShowBootDiagnostics(VOID)");
        must(scan,"TempMenuEntry->Tag = TAG_DRIVER_DIAGNOSTICS;");
        must(scan,"if (GlobalConfig.DriverDiagnostics)");
        must(menu,"BootGetStartupTimingLine(Line)");
        const auto main_paint=segment(menu,"static VOID PaintAll(","} // static VOID PaintAll()");
        must_not(main_paint,"BootGetDriverDiagnosticLine(");
        must_not(main_paint,"BootGetStartupTimingLine(");
        const std::string detail=menu.find("} // VOID ShowBootDiagnostics(")!=std::string::npos?
            segment(menu,"VOID ShowBootDiagnostics(VOID)","} // VOID ShowBootDiagnostics("):
            segment(menu,"VOID ShowBootDiagnostics(VOID)","// Check each filename in FilenameList");
        must(detail,"RunMenu(&Diagnostics, NULL);");
        must(detail,"FreeList((VOID ***)&Diagnostics.InfoLines");
        must(detail,"MyFreePool(Diagnostics.Entries)");
        must(drivers,"Line >= 15");
        must(drivers,"Result[50] = 0");
        const auto functions=segment(drivers,"static UINTN DiagDriverAttempts",
                                     "EFI_GUID gMyEfiLoadedImageProtocolGuid");
        for(const char* unsafe:{"SetVariable","EFI_FILE_MODE_WRITE","CreateFile",
                                "WriteToLog","StartLogging"})
            must_not(functions,unsafe);
        must(drivers,"ConnectFilesystemBlockDevices();");
        must(drivers,"ConnectAllDriversToAllControllers();");
        must(launch,"ConnectFilesystemDriver(ChildImageHandle);");
        demand(!fs::exists(root/"tests/verify-driver-diagnostics.py"),
               "Retired Python UEFI diagnostics verifier has returned");
        std::cout<<"Boot RAM-only EFI diagnostic source contract passed (native C++17)\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"Boot firmware diagnostics contract failed: "<<error.what()<<'\n';
        return 1;
    }
}
