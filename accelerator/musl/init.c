// SPDX-License-Identifier: GPL-3.0-or-later
#include <stddef.h>
#define PRIVATE __attribute__((visibility("hidden")))
PRIVATE unsigned __trames_native_enabled;
PRIVATE unsigned __trames_native_features;
// Called only after dynamic self-relocation (or during static __init_libc).
// No TLS, errno, libc calls, allocation, syscalls or constructors are needed.
PRIVATE void __trames_native_init(const size_t *auxv) {
    unsigned enabled=0, features=0;
    for (size_t i=0; auxv[i]; i+=2) {
        if (auxv[i]==0x54520001UL) enabled=auxv[i+1]==0x10001UL;
        if (auxv[i]==0x54520002UL) features=(unsigned)auxv[i+1];
    }
    __trames_native_enabled=enabled;
    __trames_native_features=enabled ? features : 0;
}
