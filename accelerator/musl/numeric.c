// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#define PRIVATE __attribute__((visibility("hidden")))
extern PRIVATE unsigned __trames_numeric_enabled;
extern PRIVATE double trames_orig_strtod(const char *, char **);
extern PRIVATE float trames_orig_strtof(const char *, char **);
struct output { uintptr_t end, error; };
struct result { uintptr_t bits, fallback; };
extern PRIVATE struct result trames_numeric_step(uintptr_t, uintptr_t, uintptr_t);
static int available(void) {
    if (!__trames_numeric_enabled) return 0;
    // Native conversion uses hardware binary64 at nearest rounding. Preserve
    // original behavior for explicitly selected guest rounding directions.
    uintptr_t fpcr; __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    return !(fpcr & 0xc00000UL);
}
double strtod(const char *restrict s, char **restrict end) {
    if (available()) {
        struct output out;
        struct result r=trames_numeric_step((uintptr_t)s,(uintptr_t)&out,1);
        if (!r.fallback) {
            if (out.error) errno=(int)out.error;
            if (end) *end=(char *)out.end;
            union { uintptr_t bits; double value; } value={.bits=r.bits};return value.value;
        }
    }
    return trames_orig_strtod(s,end);
}
float strtof(const char *restrict s, char **restrict end) {
    if (available()) {
        struct output out;
        struct result r=trames_numeric_step((uintptr_t)s,(uintptr_t)&out,0);
        if (!r.fallback) {
            if (out.error) errno=(int)out.error;
            if (end) *end=(char *)out.end;
            union { uint32_t bits; float value; } value={.bits=(uint32_t)r.bits};return value.value;
        }
    }
    return trames_orig_strtof(s,end);
}
