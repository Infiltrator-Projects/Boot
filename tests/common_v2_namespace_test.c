/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../boot/common_v2_namespace.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Deliberately minimal callback fixture. There is no format decoder or
 * filesystem implementation here: exercise the host's path contracts. */
static const struct {const char *p,*target;uint32_t type;} nodes[]={
    {"/",NULL,IFS_FS_ENTRY_DIRECTORY},{"/dir",NULL,IFS_FS_ENTRY_DIRECTORY},
    {"/dir/file",NULL,IFS_FS_ENTRY_FILE},{"/file",NULL,IFS_FS_ENTRY_FILE},
    {"/dir/relative","file",IFS_FS_ENTRY_LINK},{"/absolute","/dir/file",IFS_FS_ENTRY_LINK},
    {"/to-dir","/dir",IFS_FS_ENTRY_LINK},{"/dir/parent","../file",IFS_FS_ENTRY_LINK},
    {"/chain","to-dir/relative",IFS_FS_ENTRY_LINK},{"/cycle","cycle",IFS_FS_ENTRY_LINK},
    {"/bad","/dir/missing",IFS_FS_ENTRY_LINK},{"/escape","../../file",IFS_FS_ENTRY_LINK},
    {"/bad-utf8","\xc0\xaf",IFS_FS_ENTRY_LINK},{"/backslash","dir\\file",IFS_FS_ENTRY_LINK},
    {"/dir/complex","/to-dir/../file",IFS_FS_ENTRY_LINK}
};
static int stat(void *v,const char *path,IfsFsEntryV2 *e)
{
    (void)v;
    for(size_t i=0;i<sizeof(nodes)/sizeof(nodes[0]);i++)if(!strcmp(path,nodes[i].p)){
        const char *leaf=strrchr(path,'/')+1;
        e->type=nodes[i].type;e->size_bytes=e->type==IFS_FS_ENTRY_FILE?5:0;
        e->name_bytes=(uint32_t)strlen(leaf);
        memcpy(e->name,leaf,e->name_bytes+1);return IFS_FS_OK;
    }
    if(!strcmp(path,"/unterminated")){
        e->type=IFS_FS_ENTRY_FILE;e->name_bytes=3;memcpy(e->name,"bad",3);return IFS_FS_OK;
    }
    return IFS_FS_NOT_FOUND;
}
static int link(void *v,const char *path,char *out,uint32_t cap,uint32_t *n)
{
    (void)v;
    for(size_t i=0;i<sizeof(nodes)/sizeof(nodes[0]);i++)if(nodes[i].target&&!strcmp(path,nodes[i].p)){
        *n=(uint32_t)strlen(nodes[i].target);if(*n>=cap)return IFS_FS_NO_SPACE;
        memcpy(out,nodes[i].target,*n);return IFS_FS_OK;
    }
    return IFS_FS_NOT_FOUND;
}
static const IfsFsCoreOpsV2 core={.stat=stat};
static const IfsFsMetadataOpsV2 meta={.header={.capabilities=IFS_FS_META_READLINK},.readlink=link};
static void check(const char *path,const char *expected)
{
    IfsFsEntryV2 e;char name[BOOT_V2_PATH_BYTES],resolved[BOOT_V2_PATH_BYTES];
    int rc=boot_v2_resolve(&core,&meta,NULL,path,resolved,&e,name);
    if(expected){assert(rc==IFS_FS_OK);assert(!strcmp(resolved,expected));assert(e.type==IFS_FS_ENTRY_FILE);}
    else assert(rc!=IFS_FS_OK);
}
int main(void)
{
    check("/file","/file");check("/absolute","/dir/file");check("/dir/relative","/dir/file");
    check("/to-dir/file","/dir/file");check("/dir/parent","/file");check("/chain","/dir/file");
    check("/escape","/file");check("/dir/complex","/file");
    check("/bad",NULL);check("/cycle",NULL);check("/bad-utf8",NULL);check("/backslash",NULL);
    check("/unterminated",NULL);check("/file/child",NULL);
    IfsFsEntryV2 e;char name[BOOT_V2_PATH_BYTES],resolved[BOOT_V2_PATH_BYTES];
    assert(boot_v2_resolve(&core,NULL,NULL,"/absolute",resolved,&e,name)==IFS_FS_UNSUPPORTED);
    /* STAT and directory enumeration share the same descriptor checks. */
    e=(IfsFsEntryV2){.structure_bytes=sizeof(e),.name=name,.name_capacity=sizeof(name),.type=IFS_FS_ENTRY_FILE};
    name[0]=0;
    assert(boot_v2_entry_valid(&e,name,0)&&!boot_v2_entry_valid(&e,name,1));
    memcpy(name,"a/b",4);e.name_bytes=3;
    assert(boot_v2_entry_valid(&e,name,0)&&!boot_v2_entry_valid(&e,name,1));
    memcpy(name,"a\\b",4);
    assert(!boot_v2_entry_valid(&e,name,1));
    memcpy(name,"a\0b",4);
    assert(!boot_v2_entry_valid(&e,name,0)&&!boot_v2_entry_valid(&e,name,1));
    memcpy(name,"\xc0\xaf",3);e.name_bytes=2;
    assert(!boot_v2_entry_valid(&e,name,0)&&!boot_v2_entry_valid(&e,name,1));
    memcpy(name,"abc",4);e.name_bytes=3;
    assert(boot_v2_entry_valid(&e,name,0)&&boot_v2_entry_valid(&e,name,1));
    e.name_capacity--;
    assert(!boot_v2_entry_valid(&e,name,1));
    puts("BOOT bounded Common V2 namespace links, malformed names and cycles PASS");return 0;
}
