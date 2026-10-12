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
PRIVATE void *trames_orig_memchr(const void *, int, size_t);
PRIVATE void *trames_orig_memrchr(const void *, int, size_t);
PRIVATE size_t trames_orig_strnlen(const char *, size_t);
PRIVATE int trames_orig_strcmp(const char *, const char *);
PRIVATE int trames_orig_strncmp(const char *, const char *, size_t);
#endif
#define memcpy trames_orig_memcpy
#define memmove trames_orig_memmove
#define memset trames_orig_memset
#define memcmp trames_orig_memcmp
#define strlen trames_orig_strlen
#define memchr trames_orig_memchr
#define memrchr trames_orig_memrchr
#define strnlen trames_orig_strnlen
#define strcmp trames_orig_strcmp
#define strncmp trames_orig_strncmp
