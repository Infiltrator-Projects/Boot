/*
 * BOOT / Icon regression harness
 *
 * Host harness for production SetLoaderDefaults(),
 * GuessLinuxDistribution(), ParseReleaseFile() and
 * AppendFirmwareOSIconHints(). tests/host-icon-detection.sh extracts those
 * exact function bodies into an include file; the mocks represent disk
 * metadata and resource lookup only.
 *
 * No firmware variable or partition is modified.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

typedef wchar_t CHAR16;
typedef unsigned long UINTN;
typedef int BOOLEAN;
typedef int EFI_STATUS;
typedef void VOID;
#define TRUE 1
#define FALSE 0
#define IN
#define OUT
#define EFI_SUCCESS 0
#define DISK_KIND_NET 3
#define ICON_SIZE_BIG 2
#define GRAPHICS_FOR_LINUX 1
#define GRAPHICS_FOR_WINDOWS 2
#define GRAPHICS_FOR_GRUB 4
#define GRAPHICS_FOR_ELILO 8
#define GRAPHICS_FOR_OSX 16
#define MACOSX_LOADER_PATH L"System\\Library\\CoreServices\\boot.efi"
#define LOG(...) ((void)0)
typedef struct { const CHAR16 *name; } EG_IMAGE;
typedef struct {
    const CHAR16 *FsName, *PartName, *release;
    void *RootDir;
    EG_IMAGE *VolIconImage;
    int DiskKind;
} REFIT_VOLUME;
typedef struct { CHAR16 *Title; EG_IMAGE *Image; CHAR16 ShortcutLetter; } MENU_ENTRY;
typedef struct {
    MENU_ENTRY me;
    char OSType;
    CHAR16 *LoadOptions;
    UINTN UseGraphicsMode;
} LOADER_ENTRY;
typedef struct { CHAR16 *Buffer; size_t offset; } REFIT_FILE;
static struct { UINTN IconSizes[4]; CHAR16 *LinuxPrefixes; UINTN GraphicsFor; } GlobalConfig = {
    {0,0,128,0}, L"vmlinuz,bzImage,kernel", 31
};
static EG_IMAGE availableIcons[] = {
    {L"linuxmint"}, {L"linux"}, {L"win8"}, {L"win"},
    {L"boot"}, {L"ubuntu"}, {L"fedora"}, {L"mac"},
    {L"grub"}, {L"unknown"}, {L"custom"}
};
static CHAR16 *copy_n(const CHAR16 *s, size_t n) {
    CHAR16 *p=(CHAR16 *)malloc((n+1)*sizeof(*p));
    if (!p) abort();
    wmemcpy(p,s,n);p[n]=0;return p;
}
static CHAR16 *StrDuplicate(const CHAR16 *s) {
    return s?copy_n(s,wcslen(s)):NULL;
}
static void MyFreePool(void *p) { free(p); }
static BOOLEAN MyStriCmp(const CHAR16 *a, const CHAR16 *b) {
    if (!a || !b) return FALSE;
    while (*a && *b) {
        if (towlower(*a++)!=towlower(*b++))return FALSE;
    }
    return !*a && !*b;
}
static BOOLEAN StriSubCmp(const CHAR16 *small, const CHAR16 *big) {
    size_t n;
    if (!small || !big) return FALSE;
    n=wcslen(small);
    for (const CHAR16 *p=big; *p; ++p) {
        size_t j=0;
        for (;j<n && p[j];++j)
            if(towlower(p[j])!=towlower(small[j]))break;
        if(j==n)return TRUE;
    }
    return FALSE;
}
static VOID MergeStrings(CHAR16 **target, CHAR16 *extra, CHAR16 separator) {
    size_t a,b;
    CHAR16 *s;
    if (!target || !extra || !*extra) return;
    a=(*target)?wcslen(*target):0; b=wcslen(extra);
    s=(CHAR16*)malloc((a+b+2)*sizeof(CHAR16));
    if(!s)abort();
    if(a)wmemcpy(s,*target,a);
    if(a && separator)s[a++]=separator;
    wmemcpy(s+a,extra,b);
    s[a+b]=0;
    free(*target);*target=s;
}
static VOID MergeWords(CHAR16 **target, const CHAR16 *source, CHAR16 sep) {
    CHAR16 *s,*p,*begin;
    if(!source)return;
    s=StrDuplicate(source);p=s;begin=s;
    for (;;) {
        if(!*p || *p==L' ' || *p==L'_' || *p==L':' || *p==L'-') {
            CHAR16 saved=*p;
            *p=0;
            if(*begin)MergeStrings(target,begin,sep);
            if(!saved)break;
            begin=p+1;
        }
        ++p;
    }
    free(s);
}
static BOOLEAN IsInSubstring(CHAR16 *big, CHAR16 *list) {
    const CHAR16 *p=list;
    if(!big || !list)return FALSE;
    while(*p) {
        const CHAR16 *start=p;
        while(*p && *p!=L',')++p;
        if(p>start) {
            CHAR16 *token=copy_n(start,(size_t)(p-start));
            BOOLEAN found=StriSubCmp(token,big);
            free(token);
            if(found)return TRUE;
        }
        if(*p)++p;
    }
    return FALSE;
}
static CHAR16 *Basename(CHAR16 *path) {
    const CHAR16 *last=wcsrchr(path,L'\\');
    return StrDuplicate(last?last+1:path);
}
static CHAR16 *FindPath(CHAR16 *path) {
    const CHAR16 *last=wcsrchr(path,L'\\');
    return copy_n(path,last?(size_t)(last-path):0);
}
static CHAR16 *StripEfiExtension(CHAR16 *filename) {
    const CHAR16 *dot=wcsrchr(filename,L'.');
    return copy_n(filename,dot?(size_t)(dot-filename):wcslen(filename));
}
static CHAR16 *FindLastDirName(CHAR16 *path) {
    const CHAR16 *end=wcsrchr(path,L'\\'),*start;
    if(!end)return StrDuplicate(L"");
    start=end;
    while(start>path && start[-1]!=L'\\')--start;
    return copy_n(start,(size_t)(end-start));
}
static EG_IMAGE *egLoadIconAnyType(void *root, CHAR16 *path, CHAR16 *name, UINTN size) {
    (void)root;(void)path;(void)name;(void)size;return NULL;
}
static EG_IMAGE *egCopyImage(EG_IMAGE *icon) { return icon; }
static CHAR16 *GetMainLinuxOptions(CHAR16 *path, REFIT_VOLUME *volume) {
    (void)path;(void)volume;return NULL;
}
static EG_IMAGE *LoadOSIcon(CHAR16 *hints, CHAR16 *fallback, BOOLEAN logo) {
    CHAR16 *s,*token,*state;
    (void)logo;
    if(hints) {
        s=StrDuplicate(hints);
        for(token=wcstok(s,L",",&state);token;token=wcstok(NULL,L",",&state)) {
            for(size_t i=0;i<sizeof(availableIcons)/sizeof(availableIcons[0]);++i) {
                if(MyStriCmp(token,availableIcons[i].name)) {
                    free(s);return &availableIcons[i];
                }
            }
        }
        free(s);
    }
    if(fallback) {
        for(size_t i=0;i<sizeof(availableIcons)/sizeof(availableIcons[0]);++i)
            if(MyStriCmp(fallback,availableIcons[i].name))return &availableIcons[i];
    }
    return NULL;
}
static BOOLEAN FileExists(void *root, CHAR16 *filename) {
    REFIT_VOLUME *v=(REFIT_VOLUME*)root;
    return v && v->release && MyStriCmp(filename,L"etc\\os-release");
}
/*
 * Supply fake etc/os-release data as a newly allocated UTF-16 buffer.
 * Production classification consumes it through token parsing; no actual EFI
 * filesystem I/O or firmware variable access occurs.
 */
static EFI_STATUS ReadFile(void *root, CHAR16 *filename, REFIT_FILE *file, UINTN *size) {
    REFIT_VOLUME *v=(REFIT_VOLUME*)root;
    if(!v || !v->release || !MyStriCmp(filename,L"etc\\os-release"))return 1;
    file->Buffer=StrDuplicate(v->release);file->offset=0;
    if(size)*size=wcslen(file->Buffer)*sizeof(CHAR16);
    return EFI_SUCCESS;
}
/*
 * This deliberately emulates ReadTokenLine/FreeTokenLine ownership: freeing
 * a line resets its caller's TokenCount to zero. The old release-file loop
 * incorrectly examined that reset value rather than reading another line.
 */
static UINTN ReadTokenLine(REFIT_FILE *file, CHAR16 ***tokens) {
    const CHAR16 *start,*end;
    CHAR16 *line,*key,*value,*eq;
    *tokens=NULL;
    if(!file || !file->Buffer)return 0;
    while(file->Buffer[file->offset]) {
        start=file->Buffer+file->offset;
        end=start;
        while(*end && *end!=L'\n' && *end!=L'\r')++end;
        file->offset+=(size_t)(end-start);
        while(file->Buffer[file->offset]==L'\r' || file->Buffer[file->offset]==L'\n')++file->offset;
        line=copy_n(start,(size_t)(end-start));
        key=line;
        while(iswspace(*key))++key;
        if(!*key || *key==L'#'){free(line);continue;}
        eq=wcschr(key,L'=');
        if(!eq){free(line);continue;}
        *eq++=0;
        value=eq;
        while(iswspace(*value))++value;
        if(*value==L'"') {
            ++value;
            CHAR16 *tail=wcsrchr(value,L'"');
            if(tail)*tail=0;
        }
        *tokens=(CHAR16**)calloc(2,sizeof(CHAR16*));
        if(!*tokens)abort();
        (*tokens)[0]=StrDuplicate(key);
        (*tokens)[1]=StrDuplicate(value);
        free(line);
        return 2;
    }
    return 0;
}
static VOID FreeTokenLine(CHAR16 ***tokens, UINTN *count) {
    if(tokens && *tokens) {
        free((*tokens)[0]);free((*tokens)[1]);free(*tokens);*tokens=NULL;
    }
    if(count)*count=0;
}

/* The script inserts the unmodified production function bodies here. */
#include "icon-detection-production.inc"

/*
 * Invoke the extracted production SetLoaderDefaults against deterministic
 * disk and icon metadata. Assert both icon choice and OSType so an
 * accidental match of a generic title is insufficient.
 */
static void expect_icon(const char *name,const CHAR16 *path,const CHAR16 *release,
                        const CHAR16 *expected,char expectedType) {
    REFIT_VOLUME volume={0};
    LOADER_ENTRY entry={0};
    volume.RootDir=&volume;
    volume.FsName=L"ext4";
    volume.PartName=L"Linux filesystem";
    volume.release=release;
    entry.me.Title=L"Test EFI loader";
    SetLoaderDefaults(&entry,(CHAR16*)path,&volume);
    if(!entry.me.Image || !MyStriCmp(entry.me.Image->name,expected) ||
       (expectedType && entry.OSType!=expectedType)) {
        fwprintf(stderr,L"%hs: expected icon %ls and OS type %lc, got %ls and %lc\n",
                 name,expected,(wint_t)expectedType,
                 entry.me.Image?entry.me.Image->name:L"(null)",(wint_t)entry.OSType);
        exit(1);
    }
    printf("PASS %s\n",name);
    free(entry.LoadOptions);
}
/*
 * Exercise firmware Boot#### label-to-icon hints without a file-backed
 * loader. The label is untrusted descriptive metadata, not proof that an EFI
 * executable belongs to the claimed operating system.
 */
static void expect_firmware(const CHAR16 *title,const CHAR16 *expected) {
    CHAR16 *hints=NULL;
    EG_IMAGE *icon;
    AppendFirmwareOSIconHints(&hints,(CHAR16*)title);
    MergeWords(&hints,(CHAR16*)title,L',');
    icon=LoadOSIcon(hints,L"unknown",FALSE);
    if(!icon || !MyStriCmp(icon->name,expected)) {
        fwprintf(stderr,L"Firmware %ls: expected %ls, got %ls\n",title,expected,
                 icon?icon->name:L"(null)");
        exit(1);
    }
    printf("PASS firmware label\n");
    free(hints);
}
/*
 * Enumerate the regression cases across Linux, Windows, macOS, GRUB and
 * BOOT. The production functions are included from a source extraction, not
 * recreated inside this fixture.
 */
int main(void) {
    const CHAR16 *mintRelease=
        L"NAME=\"Linux Mint\"\n"
        L"VERSION=\"22.2 (Zara)\"\n"
        L"ID=linuxmint\n"
        L"ID_LIKE=\"ubuntu debian\"\n"
        L"PRETTY_NAME=\"Linux Mint 22.2\"\n";
    expect_icon("Mint 800GiB ext4 direct kernel",L"\\boot\\vmlinuz-7.0.0-42-generic",
                mintRelease,L"linuxmint",'L');
    expect_icon("Generic kernel on boot directory",L"\\boot\\vmlinuz-7.0.0-42-generic",
                NULL,L"linux",'L');
    expect_icon("Windows EFI boot manager",L"EFI\\Microsoft\\Boot\\bootmgfw.efi",
                NULL,L"win8",'W');
    expect_icon("Windows backup loader",L"EFI\\Microsoft\\Boot\\bkpbootmgfw.efi",
                NULL,L"win8",'W');
    expect_icon("BOOT's own executable",L"EFI\\InfiltratorOS\\BOOT\\BOOTX64.EFI",
                NULL,L"boot",'R');
    expect_icon("macOS boot.efi",L"System\\Library\\CoreServices\\boot.efi",
                NULL,L"mac",'M');
    expect_icon("GRUB in Ubuntu directory",L"EFI\\ubuntu\\grubx64.efi",
                NULL,L"grub",'G');
    expect_firmware(L"Windows Boot Manager",L"win8");
    expect_firmware(L"Linux Mint Boot Manager",L"linuxmint");
    expect_firmware(L"BOOT Boot Manager",L"boot");
    puts("Production OS icon autodetection regressions passed");
    return 0;
}
