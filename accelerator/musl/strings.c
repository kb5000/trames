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
extern PRIVATE void *trames_orig_memrchr(const void *,int,size_t);
// Original musl's aligned-word HASZERO search, capped at a 64-byte prefix.
// Little-endian AArch64: the first set byte is the first actual NUL/match.
static const char *prefix_stop(const char *s,int c) {
    typedef size_t __attribute__((__may_alias__)) word;
    const unsigned char *p=(const void *)s,*start=p;
    const size_t ones=(size_t)-1/255,highs=ones*128,k=ones*(unsigned char)c;
    while((uintptr_t)p & (sizeof(word)-1)){if(!*p || *p==(unsigned char)c)return (const char *)p;p++;}
    while((size_t)(p-start)+sizeof(word)<=64){
        size_t v=*(const word *)p,x=v^k;
        size_t stop=((v-ones)&~v&highs)|((x-ones)&~x&highs);
        if(stop)return (const char *)p+(size_t)__builtin_ctzl(stop)/8;
        p+=sizeof(word);
    }
    while(p-start<64){if(!*p || *p==(unsigned char)c)return (const char *)p;p++;}
    return 0;
}
char *strchr(const char *s,int c) {
    if(available(11)) {
        const char *p=prefix_stop(s,c);if(p)return *(const unsigned char *)p==(unsigned char)c?(char *)p:0;
        struct result r=trames_call_11((uintptr_t)s,(uintptr_t)c,0);if(!r.fallback)return (char *)r.value;
    }
    return trames_orig_strchr(s,c);
}
char *strrchr(const char *s,int c) {
    if(available(12)) {
        const char *end=prefix_stop(s,0);if(end)return trames_orig_memrchr(s,c,end-s+1);
        struct result r=trames_call_12((uintptr_t)s,(uintptr_t)c,0);if(!r.fallback)return (char *)r.value;
    }
    return trames_orig_strrchr(s,c);
}
char *__strchrnul(const char *s,int c) {
    if(available(13)) {
        const char *p=prefix_stop(s,c);if(p)return (char *)p;
        struct result r=trames_call_13((uintptr_t)s,(uintptr_t)c,0);if(!r.fallback)return (char *)r.value;
    }
    return trames_orig___strchrnul(s,c);
}
weak_alias(__strchrnul,strchrnul);
char *strcpy(char *restrict d,const char *restrict s) {
    if(available(14) && !prefix_stop(s,0)){struct result r=trames_call_14((uintptr_t)d,(uintptr_t)s,0);if(!r.fallback)return (char *)r.value;}
    return trames_orig_strcpy(d,s);
}
char *__stpcpy(char *restrict d,const char *restrict s) {
    if(available(15) && !prefix_stop(s,0)){struct result r=trames_call_15((uintptr_t)d,(uintptr_t)s,0);if(!r.fallback)return (char *)r.value;}
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
