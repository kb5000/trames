// SPDX-License-Identifier: GPL-3.0-or-later
// Ordinary guest ELF wrapper; original musl implementations remain as fallback.
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
static unsigned enabled;
__attribute__((constructor)) static void negotiate(void) {
    int saved = errno;
    enabled = getauxval(0x54520001UL) == 0x10001UL;
    errno = saved;
}
void *memcpy(void *d, const void *s, size_t n) {
    if (enabled) { struct result r = trames_call_1((uintptr_t)d,(uintptr_t)s,n); if (!r.fallback) return (void *)r.value; }
    return trames_orig_memcpy(d,s,n);
}
void *memmove(void *d, const void *s, size_t n) {
    if (enabled) { struct result r = trames_call_2((uintptr_t)d,(uintptr_t)s,n); if (!r.fallback) return (void *)r.value; }
    return trames_orig_memmove(d,s,n);
}
void *memset(void *d, int c, size_t n) {
    if (enabled) { struct result r = trames_call_3((uintptr_t)d,(uintptr_t)c,n); if (!r.fallback) return (void *)r.value; }
    return trames_orig_memset(d,c,n);
}
int memcmp(const void *a, const void *b, size_t n) {
    if (enabled) { struct result r = trames_call_4((uintptr_t)a,(uintptr_t)b,n); if (!r.fallback) return (int)r.value; }
    return trames_orig_memcmp(a,b,n);
}
size_t strlen(const char *s) {
    if (enabled) { struct result r = trames_call_5((uintptr_t)s,0,0); if (!r.fallback) return r.value; }
    return trames_orig_strlen(s);
}
