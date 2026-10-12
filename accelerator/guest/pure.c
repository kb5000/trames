// SPDX-License-Identifier: GPL-3.0-or-later
// Ordinary guest ELF wrapper; original musl implementations remain as fallback.
#define _GNU_SOURCE
#include <string.h>
#include <stdint.h>
#include <sys/auxv.h>
#include <errno.h>
struct result { uintptr_t value, fallback; };
#define DECLARE(n) extern struct result trames_call_##n(uintptr_t, uintptr_t, size_t)
DECLARE(1); DECLARE(2); DECLARE(3); DECLARE(4); DECLARE(5);
extern void *trames_orig_memcpy(void *, const void *, size_t);
extern void *trames_orig_memmove(void *, const void *, size_t);
extern void *trames_orig_memset(void *, int, size_t);
extern int trames_orig_memcmp(const void *, const void *, size_t);
extern size_t trames_orig_strlen(const char *);
#ifdef TRAMES_FULL_MUSL
extern __attribute__((visibility("hidden"))) unsigned __trames_native_enabled;
extern __attribute__((visibility("hidden"))) unsigned __trames_native_features;
#define enabled __trames_native_enabled
// The first-profile benchmark regresses on tiny calls. Keep those local.
#define sized(n) ((n) >= 128)
#else
static unsigned enabled;
#define sized(n) 1
__attribute__((constructor)) static void negotiate(void) {
    int saved = errno;
    enabled = getauxval(0x54520001UL) == 0x10001UL;
    errno = saved;
}
#endif
void *memcpy(void *d, const void *s, size_t n) {
    if (enabled && sized(n)) { struct result r = trames_call_1((uintptr_t)d,(uintptr_t)s,n); if (!r.fallback) return (void *)r.value; }
    return trames_orig_memcpy(d,s,n);
}
void *memmove(void *d, const void *s, size_t n) {
    if (enabled && sized(n)) { struct result r = trames_call_2((uintptr_t)d,(uintptr_t)s,n); if (!r.fallback) return (void *)r.value; }
    return trames_orig_memmove(d,s,n);
}
void *memset(void *d, int c, size_t n) {
    if (enabled && sized(n)) { struct result r = trames_call_3((uintptr_t)d,(uintptr_t)c,n); if (!r.fallback) return (void *)r.value; }
    return trames_orig_memset(d,c,n);
}
int memcmp(const void *a, const void *b, size_t n) {
    if (enabled && sized(n)) { struct result r = trames_call_4((uintptr_t)a,(uintptr_t)b,n); if (!r.fallback) return (int)r.value; }
    return trames_orig_memcmp(a,b,n);
}
size_t strlen(const char *s) {
#ifdef TRAMES_FULL_MUSL
    if (enabled) {
        // Bounded prefix of musl's original aligned-word HASZERO algorithm.
        // Aligned words cannot cross a page; a terminator near a guard page
        // therefore returns before any access to the following page.
        const unsigned char *p=(const void *)s;
        while ((uintptr_t)p & (sizeof(size_t)-1)) { if (!*p) return p-(const unsigned char *)s; p++; }
        typedef size_t __attribute__((__may_alias__)) word;
        const size_t ones=(size_t)-1/255, highs=ones*128;
        for (unsigned i=0;i<8;i++,p+=sizeof(word)) {
            size_t v=*(const word *)p, zero=(v-ones)&~v&highs;
            if (zero) return p-(const unsigned char *)s+(size_t)__builtin_ctzl(zero)/8;
        }
    }
#endif
    if (enabled) { struct result r = trames_call_5((uintptr_t)s,0,0); if (!r.fallback) return r.value; }
    return trames_orig_strlen(s);
}

#ifdef TRAMES_FULL_MUSL
// New entries require an explicit function bit. A profile-1 host with no bitmap
// continues to accelerate only the original five functions, without SIGILL.
DECLARE(6); DECLARE(7); DECLARE(8); DECLARE(9); DECLARE(10);
#define available(id) (__trames_native_features & (1u << ((id)-1)))
extern void *trames_orig_memchr(const void *, int, size_t);
extern void *trames_orig_memrchr(const void *, int, size_t);
extern size_t trames_orig_strnlen(const char *, size_t);
extern int trames_orig_strcmp(const char *, const char *);
extern int trames_orig_strncmp(const char *, const char *, size_t);
// Bounded original-musl HASZERO prefix for a large bound over a short string.
// Byte alignment and full-word bounds avoid reading the next protected page.
static size_t short_nul(const char *s) {
    const unsigned char *p=(const void *)s, *start=p;
    const size_t ones=(size_t)-1/255, highs=ones*128;
    typedef size_t __attribute__((__may_alias__)) word;
    while ((uintptr_t)p & (sizeof(word)-1)) { if (!*p) return p-start; p++; }
    while ((size_t)(p-start)+sizeof(word)<=64) {
        size_t v=*(const word *)p, zero=(v-ones)&~v&highs;
        if (zero) return p-start+(size_t)__builtin_ctzl(zero)/8;
        p+=sizeof(word);
    }
    while (p-start<64) { if (!*p) return p-start; p++; }
    return 64;
}
void *memchr(const void *s, int c, size_t n) {
    if (available(6) && sized(n)) { struct result r=trames_call_6((uintptr_t)s,(uintptr_t)c,n); if (!r.fallback) return (void *)r.value; }
    return trames_orig_memchr(s,c,n);
}
void *memrchr(const void *s, int c, size_t n) {
    if (available(7) && sized(n)) { struct result r=trames_call_7((uintptr_t)s,(uintptr_t)c,n); if (!r.fallback) return (void *)r.value; }
    return trames_orig_memrchr(s,c,n);
}
size_t strnlen(const char *s, size_t n) {
    if (available(8) && sized(n)) {
        size_t prefix=short_nul(s); if (prefix<64) return prefix;
        struct result r=trames_call_8((uintptr_t)s,n,0); if (!r.fallback) return r.value;
    }
    return trames_orig_strnlen(s,n);
}
int strcmp(const char *a, const char *b) {
    if (available(9)) {
        // Keep the usual short names/options in guest code. Byte loads stop at
        // the first difference/NUL even when a string ends at a guard page.
        for (unsigned i=0;i<64;i++) {
            unsigned char x=(unsigned char)a[i], y=(unsigned char)b[i];
            if (x!=y || !x) return (int)x-(int)y;
        }
        struct result r=trames_call_9((uintptr_t)a,(uintptr_t)b,0); if (!r.fallback) return (int)r.value;
    }
    return trames_orig_strcmp(a,b);
}
int strncmp(const char *a, const char *b, size_t n) {
    if (available(10) && sized(n)) {
        for (unsigned i=0;i<64;i++) {
            unsigned char x=(unsigned char)a[i], y=(unsigned char)b[i];
            if (x!=y || !x) return (int)x-(int)y;
        }
        struct result r=trames_call_10((uintptr_t)a,(uintptr_t)b,n); if (!r.fallback) return (int)r.value;
    }
    return trames_orig_strncmp(a,b,n);
}
#endif
