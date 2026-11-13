// SPDX-License-Identifier: GPL-3.0-or-later
// Native C++17 regression for Boot's deterministic, self-contained UEFI ZIP.
// Checks real bundle creation in private fixture directories, including
// required files, ordering, duplicates, missing inputs and hostile symlinks.
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
namespace fs=std::filesystem;
static void require(bool ok,const std::string& reason){
    if(!ok)throw std::runtime_error(reason);
}
static std::string trim(std::string text){
    const auto start=text.find_first_not_of(" \r\n\t");
    return start==std::string::npos?std::string():
        text.substr(start,text.find_last_not_of(" \r\n\t")-start+1);
}
static std::vector<std::uint8_t> bytes(const fs::path& path){
    std::ifstream file(path,std::ios::binary);
    require(bool(file),"Cannot read "+path.string());
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
static void write(const fs::path& path,const std::string& data){
    fs::create_directories(path.parent_path());
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    require(bool(file),"Cannot open "+path.string());
    file<<data;file.close();
    require(bool(file),"Cannot write "+path.string());
}
struct Scratch{
    fs::path root;
    Scratch(){
        auto name=(fs::temp_directory_path()/"boot-bundle-native-XXXXXXXX").string();
        std::vector<char> data(name.begin(),name.end());data.push_back('\0');
        const char* result=mkdtemp(data.data());
        require(result!=nullptr,"Cannot create private ZIP regression directory");
        root=result;
    }
    ~Scratch(){std::error_code ec;if(!root.empty())fs::remove_all(root,ec);}
    Scratch(const Scratch&)=delete;
    Scratch& operator=(const Scratch&)=delete;
};
static std::string execute(const fs::path& script,const fs::path& dist,
                           const fs::path& icons,const std::string& version,
                           const fs::path& output,const fs::path& log,bool succeed){
    const pid_t child=fork();
    require(child>=0,"Cannot fork Boot bundle generator");
    if(child==0){
        const int fd=open(log.c_str(),O_CREAT|O_TRUNC|O_WRONLY,0600);
        if(fd<0)_exit(127);
        if(dup2(fd,STDOUT_FILENO)<0||dup2(fd,STDERR_FILENO)<0)_exit(127);
        if(fd>STDERR_FILENO)close(fd);
        execl("/bin/bash","bash",script.c_str(),dist.c_str(),icons.c_str(),
              version.c_str(),output.c_str(),static_cast<char*>(nullptr));
        _exit(127);
    }
    int status=0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(true){
        const auto done=waitpid(child,&status,WNOHANG);
        if(done==child)break;
        if(done<0){
            if(errno==EINTR)continue;
            kill(child,SIGKILL);
            while(waitpid(child,&status,0)<0&&errno==EINTR){}
            throw std::runtime_error("waitpid failed");
        }
        if(std::chrono::steady_clock::now()>=deadline){
            kill(child,SIGKILL);
            while(waitpid(child,&status,0)<0&&errno==EINTR){}
            throw std::runtime_error("Boot bundle creation timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const auto log_bytes=bytes(log);
    const std::string output_log(log_bytes.begin(),log_bytes.end());
    const bool ok=WIFEXITED(status)&&WEXITSTATUS(status)==0;
    require(ok==succeed,std::string(succeed?"Valid":"Invalid")+
            " Boot ZIP fixture returned unexpected result:\n"+output_log);
    require(!succeed||fs::is_regular_file(output),
            "Successful Boot bundle was not written");
    return output_log;
}
static std::uint16_t le16(const std::vector<std::uint8_t>& d,std::size_t i){
    require(i<=d.size()&&d.size()-i>=2,"Truncated ZIP field");
    return std::uint16_t(d[i]|(std::uint16_t(d[i+1])<<8));
}
static std::uint32_t le32(const std::vector<std::uint8_t>& d,std::size_t i){
    require(i<=d.size()&&d.size()-i>=4,"Truncated ZIP field");
    return std::uint32_t(d[i])|(std::uint32_t(d[i+1])<<8)|
           (std::uint32_t(d[i+2])<<16)|(std::uint32_t(d[i+3])<<24);
}
static std::vector<std::string> zip_entries(const std::vector<std::uint8_t>& zip){
    require(zip.size()>=22,"Truncated ZIP archive");
    const std::size_t floor=zip.size()>65557?zip.size()-65557:0;
    std::size_t eocd=zip.size()-22;
    while(eocd>floor&&le32(zip,eocd)!=0x06054b50)--eocd;
    require(le32(zip,eocd)==0x06054b50,"Missing ZIP central directory");
    require(le16(zip,eocd+4)==0&&le16(zip,eocd+6)==0,
            "Multi-disk ZIP archives not permitted");
    const auto count=le16(zip,eocd+10);
    const auto directory_size=le32(zip,eocd+12);
    const auto directory_offset=le32(zip,eocd+16);
    require(std::size_t(directory_offset)+directory_size<=eocd,
            "ZIP central directory outside archive");
    std::size_t pos=directory_offset;
    std::vector<std::string> names;
    for(std::size_t index=0;index<count;index++){
        require(le32(zip,pos)==0x02014b50,"Malformed ZIP central-directory record");
        const auto name_length=le16(zip,pos+28);
        const auto extra_length=le16(zip,pos+30);
        const auto comment_length=le16(zip,pos+32);
        require(pos+46+std::size_t(name_length)+extra_length+comment_length<=zip.size(),
                "ZIP filename outside central-directory bounds");
        names.emplace_back(reinterpret_cast<const char*>(zip.data()+pos+46),name_length);
        pos+=46+std::size_t(name_length)+extra_length+comment_length;
    }
    require(pos==std::size_t(directory_offset)+directory_size,
            "ZIP central directory contains extra records");
    return names;
}
int main(int argc,char**argv){
    try{
        require(argc<=2,"Usage: test-build-uefi-bundle [BOOT_REPOSITORY_ROOT]");
        const fs::path repo=fs::absolute(argc==2?argv[1]:".");
        const auto script=repo/"scripts/build-uefi-bundle.sh";
        require(fs::is_regular_file(script),"Boot bundle builder missing");
        const auto version=trim([&]{
            const auto d=bytes(repo/"VERSION");
            return std::string(d.begin(),d.end());
        }());
        require(!version.empty(),"Missing Boot release version");
        Scratch test;
        const auto dist=test.root/"dist",icons=test.root/"icons";
        fs::create_directories(dist);fs::create_directories(icons);
        write(dist/("Boot-"+version+"-x86_64.efi"),"efi-test");
        write(dist/"boot.conf","timeout 1\n");
        for(const auto& name:{"mb-corpo-s-regular.png","mb-corpo-s-bold.png",
                               "mb-corpo-a-cond-regular.png","boot-banner.png",
                               "boot-selection-big.png","boot-selection-small.png"})
            write(dist/name,std::string("root:")+name);
        for(const auto& name:{"os_infiltratoros.png","os_infiltrator.png",
                               "os_infiltratorboot.png","func_reset.png",
                               "func_shutdown.png","func_firmware.png",
                               "os_uefi.png","os_unknown.png"})
            write(icons/name,std::string("icon:")+name);
        write(icons/"README","attribution fixture\n");
        write(icons/"licenses/COPYING","test licence text\n");
        const auto first=test.root/"first.zip",second=test.root/"second.zip",log=test.root/"builder.log";
        execute(script,dist,icons,version,first,log,true);
        execute(script,dist,icons,version,second,log,true);
        require(bytes(first)==bytes(second),
                "Identical inputs produced non-deterministic Boot ZIP bytes");
        const auto names=zip_entries(bytes(first));
        require(std::is_sorted(names.begin(),names.end()),"ZIP file order is not deterministic");
        require(std::count(names.begin(),names.end(),"EFI/BOOT/BOOTX64.EFI")==1,
                "UEFI Boot entry must exist exactly once");
        require(std::find(names.begin(),names.end(),"EFI/BOOT/icons/licenses/COPYING")!=names.end(),
                "Icon licence was not packaged");
        for(const auto& name:names)
            require(name.size()<3||
                    (name.substr(name.size()-3)!=".so"&&
                     (name.size()<8||name.substr(name.size()-8)!="_x64.efi")),
                    "Independent filesystem driver leaked into Boot ZIP: "+name);
        fs::remove(icons/"os_unknown.png");
        execute(script,dist,icons,version,test.root/"missing.zip",log,false);
        write(icons/"os_unknown.png","restored");
        write(test.root/"outside.txt","do not package this\n");
        fs::create_symlink(test.root/"outside.txt",icons/"licenses/external");
        const auto rejected=execute(script,dist,icons,version,test.root/"linked.zip",log,false);
        require(rejected.find("symbolic links are not permitted")!=std::string::npos,
                "Refused symlink must be explained as a security violation");
        require(!fs::exists(repo/"tests/test-build-uefi-bundle.py"),
                "Retired Python Boot ZIP regression returned");
        std::cout<<"BOOT_BUNDLE_REGRESSION_PASS: deterministic, fail-closed, no external links (native C++17)\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"Boot native UEFI bundle test failed: "<<error.what()<<'\n';
        return 1;
    }
}
