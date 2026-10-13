// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <string.h>
#include <stdint.h>
#define PRIVATE __attribute__((visibility("hidden")))
extern PRIVATE unsigned __trames_native_features;
#define available(id) (__trames_native_features & (1u<<((id)-1)))
struct result { uintptr_t value, fallback; };
#define DECLARE(id) extern PRIVATE struct result trames_call_##id(uintptr_t,uintptr_t,size_t)
DECLARE(11);DECLARE(12);DECLARE(13);DECLARE(14);DECLARE(15);DECLARE(16);DECLARE(17);
extern PRIVATE char *trames_orig_strchr(const char *,int);
extern PRIVATE char *trames_orig_strrchr(const char *,int);
extern PRIVATE char *trames_orig___strchrnul(const char *,int);
extern PRIVATE char *trames_orig_strcpy(char *,const char *);
extern PRIVATE char *trames_orig___stpcpy(char *,const char *);
extern PRIVATE char *trames_orig_strncpy(char *,const char *,size_t);
extern PRIVATE char *trames_orig___stpncpy(char *,const char *,size_t);
// Short searches remain ordinary guest instructions. A prefix stops at the
// first match/NUL; it never probes beyond a terminator at a guard page.
char *strchr(const char *s,int c) {
    if (available(11)) {
        for(unsigned i=0;i<64;i++){unsigned char b=s[i];if(b==(unsigned char)c)return (char *)s+i;if(!b)return 0;}
        struct result r=trames_call_11((uintptr_t)s,(uintptr_t)c,0);if(!r.fallback)return (char *)r.value;
    }
    return trames_orig_strchr(s,c);
}
char *strrchr(const char *s,int c) {
    if (available(12)) {
        char *last=0;for(unsigned i=0;i<64;i++){unsigned char b=s[i];if(b==(unsigned char)c)last=(char *)s+i;if(!b)return last;}
        struct result r=trames_call_12((uintptr_t)s,(uintptr_t)c,0);if(!r.fallback)return (char *)r.value;
    }
    return trames_orig_strrchr(s,c);
}
char *__strchrnul(const char *s,int c) {
    if (available(13)) {
        for(unsigned i=0;i<64;i++){unsigned char b=s[i];if(!b || b==(unsigned char)c)return (char *)s+i;}
        struct result r=trames_call_13((uintptr_t)s,(uintptr_t)c,0);if(!r.fallback)return (char *)r.value;
    }
    return trames_orig___strchrnul(s,c);
}
weak_alias(__strchrnul,strchrnul);
// Probe only the source. Once a terminator appears, use original musl's
// aligned copy locally; long copies get a preflighted native entry.
static int long_source(const char *s) {
    typedef size_t __attribute__((__may_alias__)) word;
    const unsigned char *p=(const void *)s,*start=p;
    const size_t ones=(size_t)-1/255, highs=ones*128;
    while((uintptr_t)p & (sizeof(word)-1)){if(!*p)return 0;p++;}
    while((size_t)(p-start)+sizeof(word)<=64){size_t v=*(const word *)p;if((v-ones)&~v&highs)return 0;p+=sizeof(word);}
    while(p-start<64){if(!*p)return 0;p++;}return 1;
}
char *strcpy(char *restrict d,const char *restrict s) {
    if(available(14) && long_source(s)){struct result r=trames_call_14((uintptr_t)d,(uintptr_t)s,0);if(!r.fallback)return (char *)r.value;}
    return trames_orig_strcpy(d,s);
}
char *__stpcpy(char *restrict d,const char *restrict s) {
    if(available(15) && long_source(s)){struct result r=trames_call_15((uintptr_t)d,(uintptr_t)s,0);if(!r.fallback)return (char *)r.value;}
    return trames_orig___stpcpy(d,s);
}
weak_alias(__stpcpy,stpcpy);
char *strncpy(char *restrict d,const char *restrict s,size_t n) {
    if(available(16) && n>=128){struct result r=trames_call_16((uintptr_t)d,(uintptr_t)s,n);if(!r.fallback)return (char *)r.value;}
    return trames_orig_strncpy(d,s,n);
}
char *__stpncpy(char *restrict d,const char *restrict s,size_t n) {
    if(available(17) && n>=128){struct result r=trames_call_17((uintptr_t)d,(uintptr_t)s,n);if(!r.fallback)return (char *)r.value;}
    return trames_orig___stpncpy(d,s,n);
}
weak_alias(__stpncpy,stpncpy);
