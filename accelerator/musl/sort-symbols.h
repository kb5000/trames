/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Only pinned upstream stdlib/qsort.c and bsearch.c fallback objects use this. */
#include <stdlib.h>
#define PRIVATE __attribute__((visibility("hidden")))
PRIVATE void trames_orig_qsort_r(void *, size_t, size_t, int (*)(const void *, const void *, void *), void *);
PRIVATE void trames_orig_qsort_r_alias(void *, size_t, size_t, int (*)(const void *, const void *, void *), void *);
PRIVATE void *trames_orig_bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
#define __qsort_r trames_orig_qsort_r
#define qsort_r trames_orig_qsort_r_alias
#define bsearch trames_orig_bsearch
// Expand renamed targets before the inner macro stringifies the alias symbol.
#undef weak_alias
#define weak_alias(old,new) TRAMES_WEAK_ALIAS(old,new)
#define TRAMES_WEAK_ALIAS(old,new) extern __typeof(old) new __attribute__((__weak__,__alias__(#old)))
