// SPDX-License-Identifier: GPL-3.0-or-later
// Native boot contract: UEFI storage transport must precede independent
// Common V2 filesystem module discovery.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
static void require(bool yes,const std::string& why){
    if(!yes)throw std::runtime_error(why);
}
static std::string read(const fs::path& p){
    std::ifstream file(p,std::ios::binary);
    require(bool(file),"Cannot read "+p.string());
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
static std::size_t where(const std::string& s,const std::string& needle){
    const auto pos=s.find(needle);
    require(pos!=std::string::npos,"Boot storage-first contract missing: "+needle);
    return pos;
}
int main(int argc,char**argv){
    try{
        require(argc<=2,"Usage: verify-storage-driver-stage [BOOT_REPOSITORY_ROOT]");
        const fs::path root=argc==2?argv[1]:".";
        const auto code=read(root/"refind/driver_support.c");
        const auto at=where(code,"BOOLEAN LoadDrivers(VOID) {");
        const auto function=code.substr(at);
        where(function,"MergeStrings(&SelfDirectory, L\"storage_x64\"");
        where(function,"MergeStrings(&SelfDirectory, L\"storage\"");
        require(where(function,"L\"storage_x64\"")<where(function,"L\"storage\""),
                "x64 transport storage directory must precede generic storage");
        require(where(function,"L\"storage\"")<
                where(function,"BootSoLoadDirectory("),
                "Storage transport scan must precede Common V2 filesystem discovery");
        require(where(function,"ConnectFilesystemBlockDevices();") <
                where(function,"BootSoLoadDirectory("),
                "Firmware partition discovery must precede the SO target enumeration");
        where(function,"if (NumStorage > 0U)");
        where(code,"StartEFIImage(SelfVolume, FileName");
        where(code,"TRUE, FALSE);");
        const auto launch=read(root/"refind/launch_efi.c");
        const auto config=read(root/"refind/config.c");
        require(code.find("DRIVER_DIRS")==std::string::npos &&
                code.find("drivers_x64")==std::string::npos &&
                config.find("scan_driver_dirs")==std::string::npos &&
                launch.find("ConnectFilesystemDriver")==std::string::npos,
                "BOOT must not discover or bind independent EFI filesystem drivers");
        require(function.find("RTSX")==std::string::npos&&
                function.find("525a")==std::string::npos,
                "Specific vendor transport exceptions must not bypass generic order");
        require(!fs::exists(root/"tests/verify-storage-driver-stage.py"),
                "Retired Python transport-stage guard still present");
        std::cout<<"Boot transport-first loading contract passed (native C++17)\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"Boot transport-first contract failed: "<<e.what()<<'\n';
        return 1;
    }
}
