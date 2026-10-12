// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <stdlib.h>
#include "callbacks.h"
#define PRIVATE __attribute__((visibility("hidden")))
extern PRIVATE unsigned __trames_callback_enabled;
extern PRIVATE unsigned __trames_bsearch_enabled;
extern PRIVATE void trames_orig_qsort_r(void *, size_t, size_t, int (*)(const void *, const void *, void *), void *);
extern PRIVATE void *trames_orig_bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));

// qsort_nr.c remains the original musl wrapper: its guest wrapper_cmp adapts the
// two-argument public qsort comparator to this three-argument internal interface.
void __qsort_r(void *base, size_t count, size_t width, int (*cmp)(const void *, const void *, void *), void *arg) {
    if (__trames_callback_enabled && count>=32) {
        uintptr_t state[TRAMES_SORT_WORDS]={TRAMES_CALLBACK_VERSION,1};
        state[TRAMES_CB_BASE]=(uintptr_t)base;
        state[TRAMES_CB_NEL]=count;
        state[TRAMES_CB_WIDTH]=width;
        int sign=0;
        for (;;) {
            struct trames_callback_result r=trames_callback_step((uintptr_t)state,(uintptr_t)(intptr_t)sign,0);
            if (r.fallback) break; // Only possible before native array mutation.
            if (r.event==0) return;
            if (r.event==1) sign=cmp((const void *)state[TRAMES_CB_LEFT],(const void *)state[TRAMES_CB_RIGHT],arg);
            // Event 2 is a native work checkpoint; no guest callback is needed.
        }
    }
    trames_orig_qsort_r(base,count,width,cmp,arg);
}
weak_alias(__qsort_r, qsort_r);

void *bsearch(const void *key, const void *base, size_t count, size_t width, int (*cmp)(const void *, const void *)) {
    if (__trames_bsearch_enabled && count>=32) {
        uintptr_t state[TRAMES_SEARCH_WORDS]={TRAMES_CALLBACK_VERSION,2};
        state[TRAMES_CB_BASE]=(uintptr_t)base;
        state[TRAMES_CB_NEL]=count;
        state[TRAMES_CB_WIDTH]=width;
        state[TRAMES_CB_KEY]=(uintptr_t)key;
        int sign=0;
        for (;;) {
            struct trames_callback_result r=trames_callback_step((uintptr_t)state,(uintptr_t)(intptr_t)sign,0);
            if (r.fallback) break;
            if (r.event==0) return (void *)state[TRAMES_CB_RESULT];
            if (r.event==1) sign=cmp((const void *)state[TRAMES_CB_LEFT],(const void *)state[TRAMES_CB_RIGHT]);
        }
    }
    return trames_orig_bsearch(key,base,count,width,cmp);
}
