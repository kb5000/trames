/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shim/stdio.h"
#include "shim/stdint.h"
#include "shim/floatscan.h"
struct trames_parse_result { uint64_t bits, consumed; int error, exhausted; };
#ifdef __APPLE__
extern int *__error(void);
#define host_errno (*__error())
#else
extern int *__errno_location(void);
#define host_errno (*__errno_location())
#endif
void trames_native_parse_float(const unsigned char *bytes, size_t len, int precision, struct trames_parse_result *result) {
    int saved_errno=host_errno;
#if defined(__aarch64__)
    uint64_t fpcr, fpsr, zero=0;
    __asm__ volatile("mrs %0, fpcr\n mrs %1, fpsr\n msr fpcr, %2\n msr fpsr, %2" : "=&r"(fpcr), "=&r"(fpsr) : "r"(zero) : "memory");
#elif defined(__x86_64__)
    unsigned csr, nearest=0x1f80; unsigned short cw=0x037f; unsigned char x87[28];
    __asm__ volatile("stmxcsr %0\n fnstenv %1\n fldcw %2\n ldmxcsr %3" : "=m"(csr), "=m"(x87) : "m"(cw), "m"(nearest) : "memory");
#else
#error Unsupported development/native architecture
#endif
    FILE f={.bytes=bytes,.len=len};
    double value=trames_native_floatscan(&f,precision,1);
    if (precision==0) {
        volatile union { float value; uint32_t bits; } output={.value=(float)value};
        result->bits=output.bits;
    } else {
        volatile union { double value; uint64_t bits; } output={.value=value};
        result->bits=output.bits;
    }
    result->consumed=f.pos>=f.origin ? f.pos-f.origin : 0;
    result->error=f.error; result->exhausted=f.exhausted;
#if defined(__aarch64__)
    __asm__ volatile("msr fpcr, %0\n msr fpsr, %1" : : "r"(fpcr), "r"(fpsr) : "memory");
#else
    __asm__ volatile("fldenv %0\n ldmxcsr %1" : : "m"(x87), "m"(csr) : "memory");
#endif
    host_errno=saved_errno;
}
