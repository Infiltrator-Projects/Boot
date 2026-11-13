// SPDX-License-Identifier: GPL-3.0-or-later
// Adversarial native C++17 Boot packaging fixture. Creates exact ZIP records
// with Unix file modes, duplicate names and traversal/symlink cases; runs the
// production DEB builder without writing outside a private scratch tree.
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
namespace fs=std::filesystem;
using Bytes=std::vector<std::uint8_t>;
static void require(bool good,const std::string& reason){
    if(!good)throw std::runtime_error(reason);
}
static std::string read(const fs::path& path){
    std::ifstream f(path,std::ios::binary);
    require(bool(f),"Cannot read Boot fixture input "+path.string());
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
static std::string trim(std::string text){
    const auto i=text.find_first_not_of(" \t\n\r");
    return i==std::string::npos?std::string():
        text.substr(i,text.find_last_not_of(" \t\n\r")-i+1);
}
static void add16(Bytes& bytes,std::uint32_t v){
    bytes.push_back(std::uint8_t(v));bytes.push_back(std::uint8_t(v>>8));
}
static void add32(Bytes& bytes,std::uint32_t v){
    add16(bytes,v);add16(bytes,v>>16);
}
static void append(Bytes& result,const std::string& text){
    result.insert(result.end(),text.begin(),text.end());
}
static std::uint32_t checksum(const std::string& text){
    std::uint32_t crc=0xffffffffU;
    for(unsigned char c:text){
        crc^=c;
        for(int k=0;k<8;k++)
            crc=(crc>>1)^((crc&1U)?0xedb88320U:0U);
    }
    return crc^0xffffffffU;
}
struct Entry{
    std::string name,body;
    std::uint32_t mode;
};
static void create_zip(const fs::path& destination,const std::vector<Entry>& entries){
    Bytes result,central;
    for(const auto& item:entries){
        require(item.name.size()<=0xffff&&item.body.size()<=0xffffffffU,
                "Test ZIP member exceeds classic ZIP limits");
        const auto size=std::uint32_t(item.body.size());
        const auto crc=checksum(item.body);
        const auto offset=std::uint32_t(result.size());
        // PK local header: store mode=0, known uncompressed size and CRC.
        add32(result,0x04034b50);add16(result,20);add16(result,0);
        add16(result,0);add16(result,0);add16(result,0);
        add32(result,crc);add32(result,size);add32(result,size);
        add16(result,std::uint32_t(item.name.size()));add16(result,0);
        append(result,item.name);append(result,item.body);
        // Version made by UNIX (3), regular file versus symbolic-link attrs.
        add32(central,0x02014b50);add16(central,(3U<<8)|20);
        add16(central,20);add16(central,0);add16(central,0);
        add16(central,0);add16(central,0);
        add32(central,crc);add32(central,size);add32(central,size);
        add16(central,std::uint32_t(item.name.size()));
        add16(central,0);add16(central,0);add16(central,0);add16(central,0);
        add32(central,item.mode<<16);add32(central,offset);
        append(central,item.name);
    }
    require(entries.size()<=0xffff&&result.size()<=0xffffffffU&&
            central.size()<=0xffffffffU,"Fixture ZIP too large");
    const auto central_offset=std::uint32_t(result.size());
    result.insert(result.end(),central.begin(),central.end());
    add32(result,0x06054b50);add16(result,0);add16(result,0);
    add16(result,std::uint32_t(entries.size()));
    add16(result,std::uint32_t(entries.size()));
    add32(result,std::uint32_t(central.size()));
    add32(result,central_offset);add16(result,0);
    std::ofstream file(destination,std::ios::binary|std::ios::trunc);
    require(bool(file),"Cannot create ZIP fixture "+destination.string());
    file.write(reinterpret_cast<const char*>(result.data()),std::streamsize(result.size()));
    file.close();
    require(bool(file),"Cannot write ZIP fixture "+destination.string());
}
struct Scratch{
    fs::path dir;
    Scratch(){
        auto pattern=(fs::temp_directory_path()/"boot-deb-policy-XXXXXXXX").string();
        std::vector<char> chars(pattern.begin(),pattern.end());
        chars.push_back('\0');
        const char* made=mkdtemp(chars.data());
        require(made!=nullptr,"Cannot create private Boot packaging fixture root");
        dir=made;
    }
    ~Scratch(){std::error_code ec;if(!dir.empty())fs::remove_all(dir,ec);}
    Scratch(const Scratch&)=delete;
    Scratch& operator=(const Scratch&)=delete;
};
static void verify_case(const fs::path& builder,const fs::path& root,
                        const std::string& version,const std::string& label,
                        const std::vector<Entry>& entries,bool success){
    const auto archive=root/(label+".zip");
    const auto output=root/label/("infiltrator-boot_"+version+"_amd64.deb");
    create_zip(archive,entries);
    const auto log=root/(label+".log");
    const pid_t pid=fork();
    require(pid>=0,"Cannot fork packaging test");
    if(pid==0){
        const int fd=open(log.c_str(),O_CREAT|O_TRUNC|O_WRONLY,0600);
        if(fd<0)_exit(127);
        if(dup2(fd,STDOUT_FILENO)<0||dup2(fd,STDERR_FILENO)<0)_exit(127);
        if(fd>STDERR_FILENO)close(fd);
        execl("/bin/bash","bash",builder.c_str(),archive.c_str(),
              output.c_str(),static_cast<char*>(nullptr));
        _exit(127);
    }
    int status=0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(true){
        const auto done=waitpid(pid,&status,WNOHANG);
        if(done==pid)break;
        if(done<0){
            if(errno==EINTR)continue;
            kill(pid,SIGKILL);
            while(waitpid(pid,&status,0)<0&&errno==EINTR){}
            throw std::runtime_error("Cannot wait for Boot packaging fixture");
        }
        if(std::chrono::steady_clock::now()>=deadline){
            kill(pid,SIGKILL);
            while(waitpid(pid,&status,0)<0&&errno==EINTR){}
            throw std::runtime_error("Boot packaging test timed out: "+label);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const bool accepted=WIFEXITED(status)&&WEXITSTATUS(status)==0;
    require(accepted==success,label+" packaging returned unexpected status:\n"+read(log));
    if(success)require(fs::is_regular_file(output),
                       label+": packaging reported success without a DEB");
    else require(!fs::exists(output),label+": rejected archive produced a DEB");
}
int main(int argc,char**argv){
    try{
        require(argc<=2,"Usage: test-build-deb-policy [BOOT_SOURCE_ROOT]");
        const fs::path repo=fs::absolute(argc==2?argv[1]:".");
        const auto version=trim(read(repo/"VERSION"));
        const auto builder=repo/"packaging/build-deb.sh";
        require(!version.empty()&&fs::is_regular_file(builder),
                "Boot packaging sources missing");
        const std::uint32_t regular=0100000|0644;
        const std::vector<Entry> base={
            {"EFI/BOOT/BOOTX64.EFI","MZ-boot-qualification",regular},
            {"EFI/BOOT/boot.conf","timeout 1\n",regular},
            {"EFI/BOOT/icons/os_infiltratoros.png","PNG-qualification",regular}
        };
        Scratch temporary;
        verify_case(builder,temporary.dir,version,"valid",base,true);
        auto modified=base;modified.push_back(base.front());
        verify_case(builder,temporary.dir,version,"duplicate",modified,false);
        verify_case(builder,temporary.dir,version,"missing-efi",
                    {base[1],base[2]},false);
        modified=base;
        modified.push_back({"EFI/BOOT/drivers_x64/foreign.efi","MZ",regular});
        verify_case(builder,temporary.dir,version,"foreign-efi",modified,false);
        modified=base;
        modified.push_back({"EFI/BOOT/../bad.txt","not-allowed",regular});
        verify_case(builder,temporary.dir,version,"traversal",modified,false);
        modified=base;
        modified.push_back({"EFI/BOOT/icons/alias.png","/etc/passwd",0120000|0777});
        verify_case(builder,temporary.dir,version,"symlink",modified,false);
        modified=base;
        modified.push_back({"/EFI/BOOT/not-relative.txt","bad",regular});
        verify_case(builder,temporary.dir,version,"absolute",modified,false);
        require(!fs::exists(repo/"tests/test-build-deb-policy.py"),
                "Retired Python adversarial DEB test returned");
        std::cout<<"BOOT_BUNDLE_POLICY_PASS: valid package and six adversarial cases (native C++17)\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"Boot native DEB policy test failed: "<<e.what()<<'\n';
        return 1;
    }
}
