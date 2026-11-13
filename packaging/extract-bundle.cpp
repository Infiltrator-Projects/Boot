// SPDX-License-Identifier: GPL-3.0-or-later
// Extract only a separately verified Boot UEFI bundle into a disposable
// Debian staging directory. ZIP parsing/decompression uses native libarchive;
// the entire inventory is checked before any member is written.
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
static void require(bool allowed,const std::string& why){
    if(!allowed)throw std::runtime_error(why);
}
static std::string lower(std::string source){
    std::transform(source.begin(),source.end(),source.begin(),[](unsigned char c){
        return static_cast<char>(std::tolower(c));
    });
    return source;
}
static bool suffix(const std::string& source,const std::string& end){
    return source.size()>=end.size()&&
           source.compare(source.size()-end.size(),end.size(),end)==0;
}
static std::vector<std::string> components(const std::string& path){
    require(!path.empty()&&path.front()!='/'&&path.find('\\')==std::string::npos,
            "unsafe bundle member: "+path);
    std::vector<std::string> segments;
    std::size_t begin=0;
    while(begin<path.size()){
        const auto end=path.find('/',begin);
        const auto part=path.substr(begin,end==std::string::npos?
                                           std::string::npos:end-begin);
        require(!part.empty()&&part!="."&&part!="..",
                "unsafe bundle member: "+path);
        segments.push_back(part);
        if(end==std::string::npos)break;
        begin=end+1;
    }
    return segments;
}
struct Item{
    std::string name;
    std::uint64_t size;
};
class Reader{
    struct archive* handle=nullptr;
public:
    explicit Reader(const fs::path& path){
        handle=archive_read_new();
        require(handle!=nullptr,"Cannot allocate native ZIP reader");
        try{
            require(archive_read_support_format_zip(handle)==ARCHIVE_OK,
                    "Native ZIP format is unavailable");
            const auto source=path.string();
            const int status=archive_read_open_filename(handle,source.c_str(),64*1024);
            require(status==ARCHIVE_OK,
                    "Cannot open Boot bundle: "+std::string(archive_error_string(handle)?
                                                         archive_error_string(handle):"unknown archive error"));
        }catch(...){
            archive_read_free(handle);
            handle=nullptr;
            throw;
        }
    }
    Reader(const Reader&)=delete;
    Reader& operator=(const Reader&)=delete;
    ~Reader(){
        if(handle){archive_read_close(handle);archive_read_free(handle);}
    }
    struct archive* get(){return handle;}
};
static std::vector<Item> inventory(const fs::path& bundle){
    Reader reader(bundle);
    std::vector<Item> files;
    std::set<std::string> names,graphic_names;
    std::uint64_t total=0;
    struct archive_entry* entry=nullptr;
    for(;;){
        const int status=archive_read_next_header(reader.get(),&entry);
        if(status==ARCHIVE_EOF)break;
        require(status==ARCHIVE_OK,"Corrupt Boot ZIP member header");
        const char* raw=archive_entry_pathname(entry);
        require(raw&&*raw,"ZIP member has an empty name");
        const std::string name(raw);
        if(suffix(name,"/")){
            // Directories do not carry firmware content, but must not be links.
            require(archive_entry_filetype(entry)==AE_IFDIR||
                    archive_entry_filetype(entry)==0,
                    "symbolic links and special directories are not permitted");
            require(archive_read_data_skip(reader.get())==ARCHIVE_OK,
                    "Cannot skip ZIP directory entry");
            continue;
        }
        require(names.insert(name).second,"duplicate bundle member names are forbidden");
        const auto path=components(name);
        require(path.size()>=3&&path[0]=="EFI"&&path[1]=="BOOT",
                "unsafe bundle member: "+name);
        const auto kind=archive_entry_filetype(entry);
        require(kind==AE_IFREG||kind==0,
                "symbolic links and special files are not permitted: "+name);
        const auto size=archive_entry_size(entry);
        require(size>=0&&std::uint64_t(size)<=64ULL*1024*1024,
                "Boot ZIP member exceeds 64 MiB: "+name);
        total+=std::uint64_t(size);
        require(total<=256ULL*1024*1024,
                "Boot ZIP expands beyond the permitted 256 MiB");
        files.push_back({name,std::uint64_t(size)});
        require(archive_read_data_skip(reader.get())==ARCHIVE_OK,
                "Unable to skip ZIP file during inventory validation");
    }
    require(names.count("EFI/BOOT/BOOTX64.EFI")&&names.count("EFI/BOOT/boot.conf"),
            "missing Boot bundle files");
    std::set<std::string> root_graphics={
        "EFI/BOOT/mb-corpo-s-regular.png","EFI/BOOT/mb-corpo-s-bold.png",
        "EFI/BOOT/mb-corpo-a-cond-regular.png","EFI/BOOT/boot-banner.png",
        "EFI/BOOT/boot-selection-big.png","EFI/BOOT/boot-selection-small.png"
    };
    std::set<std::string> executable_names;
    for(const auto& item:files){
        const auto& name=item.name;
        if(suffix(name,"_PROVENANCE.txt"))
            throw std::runtime_error("provenance metadata must not ship in a DEB: "+name);
        const std::string norm=lower(name);
        if(suffix(norm,".efi"))executable_names.insert(name);
        require(name.find("/drivers_")==std::string::npos&&
                name.find("/drivers-disabled/")==std::string::npos,
                "independently owned EFI driver shipped in Boot DEB: "+name);
        if(suffix(norm,".png")){
            const auto sep=name.find_last_of('/');
            require(sep!=std::string::npos,"invalid ZIP graphics member");
            const std::string basename=name.substr(sep+1);
            require(graphic_names.insert(basename).second,
                    "duplicate graphics filenames in Boot DEB: "+basename);
            const auto parent=name.substr(0,sep);
            require(parent=="EFI/BOOT/icons"||root_graphics.count(name),
                    "icons must ship only in icons/: "+name);
        }
    }
    require(executable_names==std::set<std::string>{"EFI/BOOT/BOOTX64.EFI"},
            "Boot must contain exactly its own EFI executable");
    return files;
}
static void extract(const fs::path& bundle,const fs::path& root,
                    const std::vector<Item>& expected){
    Reader reader(bundle);
    std::size_t index=0;
    struct archive_entry* entry=nullptr;
    std::array<char,64*1024> buffer{};
    for(;;){
        const int status=archive_read_next_header(reader.get(),&entry);
        if(status==ARCHIVE_EOF)break;
        require(status==ARCHIVE_OK,"Boot ZIP changed or became unreadable");
        const char* raw=archive_entry_pathname(entry);
        require(raw!=nullptr,"Boot ZIP entry lost its filename");
        const std::string name(raw);
        if(suffix(name,"/")){
            require(archive_read_data_skip(reader.get())==ARCHIVE_OK,
                    "Unable to skip Boot ZIP directory");
            continue;
        }
        require(index<expected.size()&&name==expected[index].name,
                "Boot ZIP inventory changed between validation and extraction");
        const auto parts=components(name);
        require(parts.size()>=3&&parts[0]=="EFI"&&parts[1]=="BOOT",
                "Unsafe Boot ZIP path during extraction");
        const auto target=root/fs::path(name);
        fs::create_directories(target.parent_path());
        require(!fs::exists(target)&&!fs::is_symlink(target),
                "Refusing to overwrite an existing staged Boot member: "+name);
        std::ofstream output(target,std::ios::binary|std::ios::trunc);
        require(bool(output),"Cannot create staged Boot file: "+name);
        std::uint64_t copied=0;
        for(;;){
            const auto count=archive_read_data(reader.get(),buffer.data(),buffer.size());
            require(count>=0,"Corrupted ZIP payload or CRC: "+name);
            if(count==0)break;
            copied+=std::uint64_t(count);
            require(copied<=expected[index].size,"ZIP decompression exceeds declared size: "+name);
            output.write(buffer.data(),std::streamsize(count));
            require(bool(output),"Cannot write staged Boot file: "+name);
        }
        output.close();
        require(bool(output)&&copied==expected[index].size,
                "Boot ZIP member size mismatch: "+name);
        fs::permissions(target,fs::perms::owner_read|fs::perms::owner_write|
                        fs::perms::group_read|fs::perms::others_read,
                        fs::perm_options::replace);
        ++index;
    }
    require(index==expected.size(),"Boot ZIP extraction omitted a verified member");
}
int main(int argc,char**argv){
    try{
        require(argc==3,"Usage: extract-bundle ZIP_FILE PRIVATE_STAGING_ROOT");
        const fs::path bundle=fs::absolute(argv[1]);
        const fs::path destination=fs::absolute(argv[2]);
        require(fs::is_regular_file(bundle),"Boot bundle is not a regular file");
        require(fs::is_directory(destination),"Boot staging directory must exist");
        const auto verified=inventory(bundle);
        extract(bundle,destination,verified);
        std::cout<<"Verified and extracted "<<verified.size()
                 <<" Boot ZIP members (native C++17/libarchive)\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"Boot native ZIP extraction rejected bundle: "<<error.what()<<'\n';
        return 1;
    }
}
