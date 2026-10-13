// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <errno.h>
#define PRIVATE __attribute__((visibility("hidden")))
extern PRIVATE unsigned __trames_integer_enabled;
extern PRIVATE long long trames_orig_strtoll(const char *, char **, int);
extern PRIVATE unsigned long long trames_orig_strtoull(const char *, char **, int);
struct output { uintptr_t end, error; };
struct result { uintptr_t bits, fallback; };
extern PRIVATE struct result trames_integer_step(uintptr_t, uintptr_t, uintptr_t);
static uintptr_t convert(const char *s, char **end, int base, unsigned is_signed) {
    if (__trames_integer_enabled) {
        struct output out;
        struct result r=trames_integer_step((uintptr_t)s,(uintptr_t)&out,(uint32_t)base|((uintptr_t)is_signed<<32));
        if (!r.fallback) {
            if (out.error) errno=(int)out.error;
            if (end) *end=(char *)out.end;
            return r.bits;
        }
    }
    return is_signed ? (uintptr_t)trames_orig_strtoll(s,end,base) : trames_orig_strtoull(s,end,base);
}
long strtol(const char *restrict s,char **restrict e,int b) { return (long)convert(s,e,b,1); }
long long strtoll(const char *restrict s,char **restrict e,int b) { return (long long)convert(s,e,b,1); }
unsigned long strtoul(const char *restrict s,char **restrict e,int b) { return convert(s,e,b,0); }
unsigned long long strtoull(const char *restrict s,char **restrict e,int b) { return convert(s,e,b,0); }
intmax_t strtoimax(const char *restrict s,char **restrict e,int b) { return strtoll(s,e,b); }
uintmax_t strtoumax(const char *restrict s,char **restrict e,int b) { return strtoull(s,e,b); }
weak_alias(strtol,__strtol_internal);
weak_alias(strtoul,__strtoul_internal);
weak_alias(strtoll,__strtoll_internal);
weak_alias(strtoull,__strtoull_internal);
weak_alias(strtoimax,__strtoimax_internal);
weak_alias(strtoumax,__strtoumax_internal);
