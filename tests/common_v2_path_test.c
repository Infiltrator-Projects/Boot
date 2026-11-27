/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../boot/common_v2_namespace.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void check_utf8_decoder(void)
{
    const char good[]="A\xc2\x80\xe0\xa0\x80\xf4\x8f\xbf\xbf";
    const uint32_t expected[]={0x41,0x80,0x800,0x10ffff};
    size_t at=0;uint32_t scalar=0;
    for(size_t i=0;i<sizeof(expected)/sizeof(expected[0]);i++){
        assert(!boot_v2_utf8_next(good,sizeof(good)-1,&at,&scalar));
        assert(scalar==expected[i]);
    }
    assert(at==sizeof(good)-1);
    assert(boot_v2_utf8_next(good,sizeof(good)-1,&at,&scalar));
    assert(at==sizeof(good)-1&&scalar==0x10ffff);
    const char *invalid[]={"\xc0\x80","\xe0\x80\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xe2\x82","\xe2\x41\x80","\0"};
    const size_t bytes[]={2,3,3,4,2,3,1};
    for(size_t i=0;i<sizeof(bytes)/sizeof(bytes[0]);i++){
        at=0;scalar=0x1234;
        assert(boot_v2_utf8_next(invalid[i],bytes[i],&at,&scalar));
        assert(at==0&&scalar==0x1234);
    }
}
int main(void)
{
    check_utf8_decoder();
    char p[128];uint16_t text[128];size_t n;
    const uint16_t abs[]={'\\','E','F','I','\\','a','.','e','f','i',0};
    const uint16_t rel[]={'.','.','\\','b','\\','.','\\','c',0};
    const uint16_t root[]={'.','.','/','.','.','/','d',0};
    const uint16_t unicode[]={0x65e5,0xd83d,0xde80,0};
    const uint16_t invalid[]={0xd800,'a',0};
    assert(!boot_v2_path("/boot",abs,p,sizeof(p))&&!strcmp(p,"/EFI/a.efi"));
    assert(!boot_v2_path("/boot/sub",rel,p,sizeof(p))&&!strcmp(p,"/boot/b/c"));
    assert(!boot_v2_path("/",root,p,sizeof(p))&&!strcmp(p,"/d"));
    assert(!boot_v2_path("/",unicode,p,sizeof(p))&&!strcmp(p,"/\xe6\x97\xa5\xf0\x9f\x9a\x80"));
    assert(!boot_v2_utf16(p+1,strlen(p+1),text,128,&n)&&n==3&&!memcmp(text,unicode,sizeof(unicode)));
    assert(boot_v2_path("/",invalid,p,sizeof(p)));
    assert(boot_v2_path("/",unicode,p,4));
    assert(boot_v2_utf16("\xc0\x80",2,text,128,&n));
    assert(boot_v2_utf16("\xed\xa0\x80",3,text,128,&n));
    assert(boot_v2_utf16("\xf4\x90\x80\x80",4,text,128,&n));
    assert(boot_v2_utf16("a\0b",3,text,128,&n));
    assert(boot_v2_utf16("abcd",4,text,4,&n));
    puts("BOOT UTF-16/UTF-8 path and volume-root normalization PASS");return 0;
}
