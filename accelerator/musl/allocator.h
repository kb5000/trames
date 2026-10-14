/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TRAMES_ALLOCATOR_H
#define TRAMES_ALLOCATOR_H
#include <stddef.h>
#include <stdint.h>
#define TRAMES_ALLOCATOR_COOKIE UINT64_C(0x1250004003a00012)
_Static_assert(UNIT==16 && IB==4 && MMAP_THRESHOLD==131052,"allocator constants");
_Static_assert(sizeof(struct group)==16 && sizeof(struct meta)==40,"group/meta layout");
_Static_assert(offsetof(struct meta,mem)==16 && offsetof(struct meta,avail_mask)==24 && offsetof(struct meta,freed_mask)==28,"meta fields");
_Static_assert(sizeof(struct malloc_context)==928 && offsetof(struct malloc_context,mmap_counter)==12 && offsetof(struct malloc_context,active)==80 && offsetof(struct malloc_context,usage_by_class)==464,"context layout");
_Static_assert(RDLOCK_IS_EXCLUSIVE==1 && USE_MADV_FREE==0,"allocator glue");
static inline int trames_allocator_layout_supported(void) {
    union { struct meta meta; uint64_t words[5]; } probe={0};
    probe.meta.last_idx=1; probe.meta.freeable=1; probe.meta.sizeclass=1; probe.meta.maplen=1;
    return probe.words[4]==UINT64_C(0x1061);
}
extern __attribute__((visibility("hidden"))) unsigned __trames_allocator_enabled;
struct trames_allocator_result { uintptr_t value, fallback; };
extern __attribute__((visibility("hidden"))) struct trames_allocator_result trames_malloc_step(uintptr_t,uintptr_t,uintptr_t);
extern __attribute__((visibility("hidden"))) struct trames_allocator_result trames_free_step(uintptr_t,uintptr_t,uintptr_t);
extern __attribute__((visibility("hidden"))) struct trames_allocator_result trames_enframe_step(uintptr_t,uintptr_t,uintptr_t);
#endif
