/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Only original fallback object files are compiled with this header. */
#ifdef __ASSEMBLER__
.hidden trames_orig_memcpy
.hidden trames_orig_memset
#else
#include <stddef.h>
#define PRIVATE __attribute__((visibility("hidden")))
PRIVATE void *trames_orig_memcpy(void *, const void *, size_t);
PRIVATE void *trames_orig_memmove(void *, const void *, size_t);
PRIVATE void *trames_orig_memset(void *, int, size_t);
PRIVATE int trames_orig_memcmp(const void *, const void *, size_t);
PRIVATE size_t trames_orig_strlen(const char *);
#endif
#define memcpy trames_orig_memcpy
#define memmove trames_orig_memmove
#define memset trames_orig_memset
#define memcmp trames_orig_memcmp
#define strlen trames_orig_strlen
