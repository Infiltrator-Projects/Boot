// SPDX-License-Identifier: GPL-3.0-or-later
// Native C++17 Boot SO loader negative regression and disposable media fixtures.
// The production ELF parser must reject every independent corruption.
// All generated files remain under the caller's private scratch directory.
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
using Bytes=std::vector<std::uint8_t>;
static void require(bool yes,const std::string& reason){
    if(!yes)throw std::runtime_error(reason);
}
static Bytes read_bytes(const fs::path& path){
    std::ifstream f(path,std::ios::binary);
    require(bool(f),"Cannot read "+path.string());
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
static void save_bytes(const fs::path& path,const Bytes& data){
    std::ofstream f(path,std::ios::binary|std::ios::trunc);
    require(bool(f),"Cannot write "+path.string());
    f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));
    f.close();
    require(bool(f),"Failed writing "+path.string());
}
static std::uint16_t u16(const Bytes& d,std::uint64_t i){
    require(i<=d.size()&&d.size()-std::size_t(i)>=2,"ELF 16-bit field outside module");
    return std::uint16_t(d[std::size_t(i)]|(std::uint16_t(d[std::size_t(i)+1])<<8));
}
static std::uint32_t u32(const Bytes& d,std::uint64_t i){
    require(i<=d.size()&&d.size()-std::size_t(i)>=4,"ELF 32-bit field outside module");
    return std::uint32_t(d[std::size_t(i)])|
          (std::uint32_t(d[std::size_t(i)+1])<<8)|
          (std::uint32_t(d[std::size_t(i)+2])<<16)|
          (std::uint32_t(d[std::size_t(i)+3])<<24);
}
static std::uint64_t u64(const Bytes& d,std::uint64_t i){
    require(i<=d.size()&&d.size()-std::size_t(i)>=8,"ELF 64-bit field outside module");
    std::uint64_t result=0;
    for(unsigned k=0;k<8;k++)result|=std::uint64_t(d[std::size_t(i)+k])<<(k*8);
    return result;
}
static Bytes encode(std::uint64_t value,unsigned width){
    Bytes data(width);
    for(unsigned k=0;k<width;k++)data[k]=std::uint8_t(value>>(k*8));
    return data;
}
static void patch(Bytes& image,std::uint64_t offset,const Bytes& replacement){
    require(offset<=image.size()&&replacement.size()<=image.size()-std::size_t(offset),
            "Malformed test mutation exceeds module buffer");
    std::copy(replacement.begin(),replacement.end(),image.begin()+std::size_t(offset));
}
struct Scratch{
    fs::path dir;
    Scratch(){
        std::string pattern=(fs::temp_directory_path()/"boot-so-negative-XXXXXXXX").string();
        std::vector<char> name(pattern.begin(),pattern.end());name.push_back('\0');
        const char* made=mkdtemp(name.data());
        require(made!=nullptr,"Cannot allocate private ELF mutation directory");
        dir=made;
    }
    ~Scratch(){std::error_code error;if(!dir.empty())fs::remove_all(dir,error);}
    Scratch(const Scratch&)=delete;
    Scratch& operator=(const Scratch&)=delete;
};
static int validate_module(const fs::path& executable,const fs::path& module){
    const pid_t pid=fork();
    require(pid>=0,"Cannot fork production ELF validator");
    if(pid==0){
        const int nullfd=open("/dev/null",O_RDWR|O_CLOEXEC);
        if(nullfd<0)_exit(127);
        if(dup2(nullfd,STDOUT_FILENO)<0||dup2(nullfd,STDERR_FILENO)<0)_exit(127);
        if(nullfd>STDERR_FILENO)close(nullfd);
        execl(executable.c_str(),executable.c_str(),"--validate",module.c_str(),
              static_cast<char*>(nullptr));
        _exit(127);
    }
    int status=0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    for(;;){
        const auto completed=waitpid(pid,&status,WNOHANG);
        if(completed==pid)break;
        if(completed<0){
            if(errno==EINTR)continue;
            kill(pid,SIGKILL);
            while(waitpid(pid,&status,0)<0&&errno==EINTR){}
            throw std::runtime_error("Cannot wait for production ELF validator");
        }
        if(std::chrono::steady_clock::now()>=deadline){
            kill(pid,SIGKILL);
            while(waitpid(pid,&status,0)<0&&errno==EINTR){}
            throw std::runtime_error("Production ELF validator timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
}
struct Section{
    std::uint32_t name=0,type=0;
    std::uint64_t offset=0,length=0;
};
struct Mutation{
    std::string name;
    std::uint64_t offset;
    Bytes bytes;
};
static void malformed(const fs::path& executable,const fs::path& module){
    const Bytes original=read_bytes(module);
    require(original.size()>=64&&original[0]==0x7f&&original[1]=='E'&&
            original[2]=='L'&&original[3]=='F'&&original[4]==2&&original[5]==1,
            "Qualified FAT16.so is not an ELF64 little-endian module");
    const std::uint64_t shoff=u64(original,40),phoff=u64(original,32);
    const std::uint16_t shentsize=u16(original,58),shnum=u16(original,60),
                        shstrndx=u16(original,62),phentsize=u16(original,54),
                        phnum=u16(original,56);
    require(shentsize>=64&&shnum>0&&shstrndx<shnum&&phentsize>=56&&phnum>0,
            "Invalid ELF section/program geometry");
    require(shoff<=original.size()&&std::uint64_t(shnum)*shentsize<=original.size()-shoff,
            "ELF section table outside module");
    require(phoff<=original.size()&&std::uint64_t(phnum)*phentsize<=original.size()-phoff,
            "ELF program table outside module");
    std::vector<Section> sections;
    for(std::uint64_t i=0;i<shnum;i++){
        const auto base=shoff+i*shentsize;
        sections.push_back({u32(original,base),u32(original,base+4),
                            u64(original,base+24),u64(original,base+32)});
    }
    const auto& names=sections[shstrndx];
    require(names.offset<=original.size()&&names.length<=original.size()-names.offset,
            "ELF section name table invalid");
    const auto section_name=[&](const Section& s){
        require(s.name<names.length,"ELF section name offset invalid");
        const auto begin=std::size_t(names.offset+s.name);
        auto end=begin;
        const auto limit=std::size_t(names.offset+names.length);
        while(end<limit&&original[end]!=0)++end;
        require(end<limit,"Unterminated ELF section name");
        return std::string(original.begin()+begin,original.begin()+end);
    };
    const Section *strings=nullptr,*rela=nullptr,*symbols=nullptr;
    for(const auto& s:sections){
        if(section_name(s)==".dynstr")strings=&s;
        if(!rela&&s.type==4&&s.length>0)rela=&s;
        if(!symbols&&s.type==11)symbols=&s;
    }
    require(strings&&rela&&symbols,"Qualified V2 ELF lacks dynamic strings, relocations or symbols");
    require(strings->offset<=original.size()&&
            strings->length<=original.size()-strings->offset,
            "ELF dynamic string table exceeds module buffer");
    // The canonical V2 export must be present. A V1-only export is an
    // intentionally malformed negative fixture, never a compatibility path.
    const std::string symbol="infiltrator_filesystem_api_v2";
    const Bytes target=[&]{
        Bytes bytes(symbol.begin(),symbol.end());
        bytes.push_back(0);
        return bytes;
    }();
    const auto search_start=original.begin()+std::size_t(strings->offset);
    const auto search_end=search_start+std::size_t(strings->length);
    const auto found=std::search(search_start,search_end,target.begin(),target.end());
    require(found!=search_end,"Canonical V2 export missing from qualified SO dynstr");
    const auto export_offset=std::uint64_t(std::distance(original.begin(),found));
    const std::string obsolete="infiltrator_filesystem_api_v1";
    Bytes obsolete_bytes(obsolete.begin(),obsolete.end());
    obsolete_bytes.push_back(0);
    require(obsolete_bytes.size()==target.size(),"Malformed V1 export fixture length mismatch");
    std::uint64_t load_header=0,load_vaddr=0;
    bool found_load=false;
    for(std::uint64_t i=0;i<phnum;i++){
        const auto base=phoff+i*phentsize;
        if(u32(original,base)==1&&(u32(original,base+4)&1U)){
            load_header=base;
            load_vaddr=u64(original,base+16);
            found_load=true;
            break;
        }
    }
    require(found_load,"Qualified ELF has no executable PT_LOAD segment");
    const std::vector<Mutation> cases={
        {"signature",0,{'N','O','P','E'}},
        {"class",4,{1}},
        {"endian",5,{2}},
        {"machine",18,encode(183,2)},
        {"executable",16,encode(2,2)},
        {"section-overflow",40,encode(UINT64_MAX,8)},
        {"program-count",56,encode(65535,2)},
        {"v1-only-entry",export_offset,obsolete_bytes},
        {"writable-code",load_header+4,encode(7,4)},
        {"segment-file-overflow",load_header+8,encode(UINT64_MAX,8)},
        {"tls",load_header,encode(7,4)},
        {"relocation-kind",rela->offset+8,encode(7,8)},
        {"relocation-code-target",rela->offset,encode(load_vaddr,8)},
        {"relocation-outside-target",rela->offset,encode(UINT64_MAX-7,8)},
        {"relocation-outside-addend",rela->offset+16,encode(UINT64_MAX,8)},
        {"relocation-duplicate",rela->offset+24,
            Bytes(original.begin()+std::size_t(rela->offset),
                  original.begin()+std::size_t(rela->offset)+8)},
        {"import",symbols->offset+24+6,Bytes(2,0)},
        {"ifunc",symbols->offset+24+4,{0x1a}}
    };
    Scratch tmp;
    const auto case_path=tmp.dir/"case.so";
    require(validate_module(executable,module)==0,"Valid module was rejected before ELF mutation test");
    for(const auto& test:cases){
        Bytes mutated=original;
        patch(mutated,test.offset,test.bytes);
        save_bytes(case_path,mutated);
        const int exit_code=validate_module(executable,case_path);
        require(exit_code==2,"Malformed module case "+test.name+
                " returned "+std::to_string(exit_code)+" (expected reject=2)");
    }
    const std::vector<std::size_t> lengths={1,7,63,64,original.size()/2,original.size()-1};
    for(const auto length:lengths){
        require(length>0&&length<original.size(),"Qualified module too short for truncation test");
        save_bytes(case_path,Bytes(original.begin(),original.begin()+length));
        const int exit_code=validate_module(executable,case_path);
        require(exit_code==2,"Truncated module accepted at "+std::to_string(length)+
                " bytes (exit "+std::to_string(exit_code)+")");
    }
    std::cout<<"BOOT-ELF-MALFORMED-REJECTION-PASS "<<cases.size()+lengths.size()<<'\n';
}
static void generate_data(const fs::path& destination){
    Bytes data(137123);
    for(std::size_t i=0;i<data.size();i++)data[i]=std::uint8_t((i*37+3)%256);
    save_bytes(destination,data);
}
static void whole_disk(const fs::path& source,const fs::path& destination){
    const Bytes volume=read_bytes(source);
    require(volume.size()%512==0&&volume.size()/512<=UINT32_MAX,
            "FAT16 test volume has invalid sector count");
    Bytes mbr(512,0);
    mbr[446+4]=6;
    const auto start=encode(2048,4),count=encode(volume.size()/512,4);
    std::copy(start.begin(),start.end(),mbr.begin()+446+8);
    std::copy(count.begin(),count.end(),mbr.begin()+446+12);
    mbr[510]=0x55;mbr[511]=0xaa;
    std::ofstream output(destination,std::ios::binary|std::ios::trunc);
    require(bool(output),"Cannot open whole disk test image");
    output.write(reinterpret_cast<const char*>(mbr.data()),512);
    output.seekp(1024*1024);
    output.write(reinterpret_cast<const char*>(volume.data()),std::streamsize(volume.size()));
    output.close();
    require(bool(output),"Cannot write whole disk test image");
}
int main(int argc,char**argv){
    try{
        if(argc==4&&std::string(argv[1])=="malformed")
            malformed(fs::absolute(argv[2]),fs::absolute(argv[3]));
        else if(argc==3&&std::string(argv[1])=="generate-data")
            generate_data(argv[2]);
        else if(argc==4&&std::string(argv[1])=="whole-disk")
            whole_disk(argv[2],argv[3]);
        else throw std::runtime_error(
            "Usage: so_fixture_test malformed VALIDATOR SO | generate-data FILE | whole-disk FAT_IMAGE WHOLE_DISK_IMAGE");
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"Boot native SO fixture failed: "<<e.what()<<'\n';
        return 1;
    }
}
