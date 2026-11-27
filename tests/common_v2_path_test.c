/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../boot/common_v2_path.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
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
