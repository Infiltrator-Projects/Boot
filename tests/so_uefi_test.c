/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "../refind/so_uefi.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"UEFI SO failure line %u: %s\n",__LINE__,#x); exit(1); } } while(0)
static FILE *media_file;
static int variable_mode;
static UINT64 set_attributes, clear_attributes;
static int fail_attributes;
static EFI_STATUS EFIAPI set_memory(BootMemoryAttributes *this, EFI_PHYSICAL_ADDRESS base,
                                     UINT64 bytes, UINT64 flags) {
    (void)this; (void)base; (void)bytes; set_attributes|=flags;
    return fail_attributes?EFI_UNSUPPORTED:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI clear_memory(BootMemoryAttributes *this, EFI_PHYSICAL_ADDRESS base,
                                       UINT64 bytes, UINT64 flags) {
    (void)this; (void)base; (void)bytes; clear_attributes|=flags;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI allocate_pool(EFI_MEMORY_TYPE type, UINTN bytes, VOID **out) {
    (void)type; *out=calloc(1,bytes); return *out?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;
}
static EFI_STATUS EFIAPI release_pool(VOID *p) { free(p); return EFI_SUCCESS; }
static EFI_STATUS EFIAPI read_blocks(EFI_BLOCK_IO_PROTOCOL *this, UINT32 id,
                                      EFI_LBA lba, UINTN bytes, VOID *buffer) {
    REQUIRE(id==this->Media->MediaId && bytes%512u==0 && ((UINTN)buffer&31u)==0);
    return fseeko(media_file,(off_t)(lba*512u),SEEK_SET)==0 &&
           fread(buffer,1,bytes,media_file)==bytes?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
static void *allocate_image(void *u,size_t bytes) {
    (void)u; void *p=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    return p==MAP_FAILED?NULL:p;
}
static void release_image(void *u,void *p,size_t bytes) { (void)u; REQUIRE(munmap(p,bytes)==0); }
static int protect_image(void *u,void *p,size_t bytes,uint32_t flags) {
    (void)u;
    return mprotect(p,bytes,((flags&4u)?PROT_READ:0)|((flags&2u)?PROT_WRITE:0)|((flags&1u)?PROT_EXEC:0));
}
static EFI_STATUS EFIAPI variable_get(CHAR16 *name,EFI_GUID *guid,UINT32 *attributes,
                                      UINTN *bytes,VOID *data) {
    (void)name; (void)guid; (void)attributes;
    if(variable_mode==2) return EFI_DEVICE_ERROR;
    if(variable_mode==3) return EFI_NOT_FOUND;
    *(UINT8 *)data=(UINT8)variable_mode; *bytes=1; return EFI_SUCCESS;
}
int main(int argc,char **argv) {
    REQUIRE(argc==4);
    EFI_BOOT_SERVICES boot={0};
    boot.AllocatePool=allocate_pool; boot.FreePool=release_pool; services=&boot;
    EFI_RUNTIME_SERVICES runtime={0}; runtime.GetVariable=variable_get;
    EFI_SYSTEM_TABLE table={0}; table.RuntimeServices=&runtime;
    for(variable_mode=0;variable_mode<4;++variable_mode)
        REQUIRE(unsigned_modules_allowed(&table)==((variable_mode==0 || variable_mode==3)?EFI_SUCCESS:EFI_SECURITY_VIOLATION));
    BootMemoryAttributes attributes={NULL,set_memory,clear_memory};
    memory_attributes=&attributes;
    REQUIRE(image_protect(NULL,(void *)4096,4096,BOOT_SO_READ|BOOT_SO_EXECUTE)==0);
    REQUIRE(set_attributes==EFI_MEMORY_RO && clear_attributes==EFI_MEMORY_XP);
    set_attributes=clear_attributes=0;
    REQUIRE(image_protect(NULL,(void *)4096,4096,BOOT_SO_READ|BOOT_SO_WRITE)==0);
    REQUIRE(set_attributes==EFI_MEMORY_XP && clear_attributes==EFI_MEMORY_RO);
    fail_attributes=1;
    REQUIRE(image_protect(NULL,(void *)4096,4096,BOOT_SO_READ)!=0);
    memory_attributes=NULL;
    FILE *module=fopen(argv[1],"rb"); struct stat st;
    REQUIRE(module && fstat(fileno(module),&st)==0);
    size_t bytes=(size_t)st.st_size; void *data=malloc(bytes);
    REQUIRE(data && fread(data,1,bytes,module)==bytes && fclose(module)==0);
    SoVolume volume={0};
    BootSoMemory memory={NULL,allocate_image,release_image,protect_image};
    REQUIRE(BootSoLoad(data,bytes,&memory,&volume.image)==0); free(data);
    media_file=fopen(argv[2],"rb");
    REQUIRE(media_file && fstat(fileno(media_file),&st)==0);
    EFI_BLOCK_IO_MEDIA media={0}; media.MediaId=17; media.MediaPresent=TRUE;
    media.BlockSize=512; media.IoAlign=32; media.LastBlock=(UINT64)st.st_size/512u-1u;
    EFI_BLOCK_IO_PROTOCOL disk={0}; disk.Media=&media; disk.ReadBlocks=read_blocks;
    volume.parent_block=&disk; volume.media=media;
    volume.io=(IfsBootIo){&volume,(uint64_t)st.st_size,512,sizeof(IfsBootIo),disk_read};
    REQUIRE(posix_memalign(&volume.bounce,32,65536u)==0);
    REQUIRE(volume.image.api->mount(&volume.io)==0);
    char label[IFS_BOOT_NAME_BYTES];
    REQUIRE(volume.image.api->volume_label(label,sizeof(label))==0);
    REQUIRE(utf8_name(label,volume.label,IFS_BOOT_NAME_BYTES)>0);
    EFI_FILE_PROTOCOL *root=NULL,*file=NULL;
    REQUIRE(open_volume(&volume.filesystem,&root)==EFI_SUCCESS && root);
    REQUIRE(root->Open(root,&file,L"\\Nested Directory\\Boot Read Test.bin",EFI_FILE_MODE_WRITE,0)==EFI_WRITE_PROTECTED && !file);
    REQUIRE(root->Open(root,&file,L"0:/Nested Directory/Boot Read Test.bin",EFI_FILE_MODE_READ,0)==EFI_INVALID_PARAMETER);
    REQUIRE(root->Open(root,&file,L"\\..\\Nested Directory\\..\\Nested Directory\\Boot Read Test.bin",EFI_FILE_MODE_READ,0)==EFI_SUCCESS);
    UINT8 buffer[65536]; UINTN got=sizeof(buffer);
    REQUIRE(file->Read(file,&got,buffer)==EFI_SUCCESS && got==sizeof(buffer));
    for(UINTN i=0;i<got;++i) REQUIRE(buffer[i]==(UINT8)(i*37u+3u));
    REQUIRE(file->SetPosition(file,7)==EFI_SUCCESS);
    got=117; REQUIRE(file->Read(file,&got,buffer)==EFI_SUCCESS && got==117);
    for(UINTN i=0;i<got;++i) REQUIRE(buffer[i]==(UINT8)((i+7u)*37u+3u));
    UINT64 position=0; REQUIRE(file->GetPosition(file,&position)==EFI_SUCCESS && position==124u);
    REQUIRE(file->SetPosition(file,~(UINT64)0)==EFI_SUCCESS);
    got=32; REQUIRE(file->Read(file,&got,buffer)==EFI_SUCCESS && got==0);
    got=0; REQUIRE(file->GetInfo(file,&file_info_guid,&got,NULL)==EFI_BUFFER_TOO_SMALL && got<sizeof(buffer));
    REQUIRE(file->GetInfo(file,&file_info_guid,&got,buffer)==EFI_SUCCESS && ((EFI_FILE_INFO *)buffer)->FileSize==137123u);
    got=16; REQUIRE(file->Write(file,&got,buffer)==EFI_WRITE_PROTECTED && got==0);
    REQUIRE(file->SetInfo(file,&file_info_guid,0,NULL)==EFI_WRITE_PROTECTED);
    REQUIRE(file->Close(file)==EFI_SUCCESS);
    REQUIRE(root->Open(root,&file,L"\\Nested Directory\\Caf\u00e9 \u00dc.bin",EFI_FILE_MODE_READ,0)==EFI_SUCCESS);
    REQUIRE(file->Close(file)==EFI_SUCCESS);
    char unicode_path[SO_PATH_BYTES]; CHAR16 unicode_roundtrip[64];
    REQUIRE(make_path((SoFile *)root,L"/Caf\u00e9 \U0001f600.bin",unicode_path));
    REQUIRE(strcmp(unicode_path,"/Caf\xc3\xa9 \xf0\x9f\x98\x80.bin")==0);
    REQUIRE(utf8_name(unicode_path,unicode_roundtrip,64)>0);
    REQUIRE(unicode_roundtrip[6]==0xd83du && unicode_roundtrip[7]==0xde00u);
    CHAR16 malformed[SO_PATH_BYTES];
    for(UINTN i=0;i<SO_PATH_BYTES;++i) malformed[i]=L'a';
    malformed[SO_PATH_BYTES-1u]=0xd800u;
    UINTN malformed_length=0;
    REQUIRE(!utf16_append(unicode_path,&malformed_length,malformed));
    got=1; REQUIRE(root->Read(root,&got,buffer)==EFI_BUFFER_TOO_SMALL);
    UINTN needed=got;
    REQUIRE(root->Read(root,&got,buffer)==EFI_SUCCESS && got==needed);
    REQUIRE(((EFI_FILE_INFO *)buffer)->Attribute&EFI_FILE_DIRECTORY);
    REQUIRE(root->SetPosition(root,0)==EFI_SUCCESS);
    got=sizeof(buffer); REQUIRE(root->Read(root,&got,buffer)==EFI_SUCCESS && got==needed);
    got=sizeof(buffer); REQUIRE(root->GetInfo(root,&filesystem_info_guid,&got,buffer)==EFI_SUCCESS);
    REQUIRE(((EFI_FILE_SYSTEM_INFO *)buffer)->ReadOnly);
    REQUIRE(root->Close(root)==EFI_SUCCESS && volume.image.api->unmount()==0);
    UINT8 one;
    REQUIRE(disk_read(&volume,volume.io.media_bytes,&one,1)!=0);
    media.MediaId++;
    REQUIRE(disk_read(&volume,0,&one,1)!=0);
    REQUIRE(fclose(media_file)==0);
    media_file=fopen(argv[3],"rb");
    REQUIRE(media_file && fstat(fileno(media_file),&st)==0);
    volume.media.MediaId=media.MediaId;
    media.LastBlock=(UINT64)st.st_size/512u-1u;
    volume.media.LastBlock=media.LastBlock;
    volume.io.media_bytes=(UINT64)st.st_size;
    REQUIRE(volume.image.api->mount(&volume.io)!=0);
    free(volume.bounce); REQUIRE(fclose(media_file)==0); BootSoUnload(&volume.image);
    puts("BOOT-SO-UEFI-PROTOCOL-UTF8-READONLY-TRUST-PASS");
    return 0;
}
