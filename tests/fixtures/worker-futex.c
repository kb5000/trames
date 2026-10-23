// SPDX-License-Identifier: GPL-3.0-or-later
// Persistent threads exercise shared aliases + atomic wake/requeue, with native
// memory work runnable throughout. Host and guest use the same C assertions.
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifdef TRAMES_HOST_ORACLE
static void *native_fill(void *p, int c, size_t n) { return memset(p, c, n); }
#else
extern void *native_fill(void *, int, size_t);
#endif
enum { ROUNDS = 64 };
static _Atomic int gate, stop, active;
static _Atomic int *aliases[2];
static int ready, done, flags;
static long futex(_Atomic int *p, int op, int val, long count, _Atomic int *to, int cmp) {
    return syscall(SYS_futex, p, op | flags, val, count, to, cmp);
}
static void event(int fd, int write_it) {
    uint64_t value = 1;
    if (write_it) assert(write(fd, &value, sizeof value) == sizeof value);
    else assert(read(fd, &value, sizeof value) == sizeof value && value == 1);
}
static void *waiter(void *arg) {
    _Atomic int *word = aliases[(uintptr_t)arg];
    for (int round = 0; round < ROUNDS; round++) {
        int value;
        while ((value = atomic_load_explicit(&gate, memory_order_acquire)) < round + 1) {
            long n = futex(&gate, FUTEX_WAIT, value, 0, NULL, 0);
            assert(n == 0 || (n == -1 && errno == EAGAIN));
        }
        event(ready, 1);
        assert(futex(word, FUTEX_WAIT_BITSET, 0, 0, NULL, 1 << (uintptr_t)arg) == 0);
        event(done, 1);
    }
    return NULL;
}
static void *memory_worker(void *unused) {
    (void)unused;
    unsigned char *p = malloc(65536); assert(p);
    unsigned i = 0;
    do {
        unsigned char c = (unsigned char)++i;
        assert(native_fill(p, c, 65536) == p);
        assert(p[0] == c && p[32768] == c && p[65535] == c);
        atomic_store_explicit(&active, 1, memory_order_release);
    } while (!atomic_load_explicit(&stop, memory_order_acquire));
    free(p);
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    int shared = !strcmp(argv[1], "shared");
    assert(shared || !strcmp(argv[1], "private"));
    flags = shared ? 0 : FUTEX_PRIVATE_FLAG;
    int fd = memfd_create("worker-futex", 0); assert(fd >= 0);
    assert(!ftruncate(fd, 4096));
    aliases[0] = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(aliases[0] != MAP_FAILED);
    aliases[1] = shared ? mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : aliases[0];
    assert(aliases[1] != MAP_FAILED && (!shared || aliases[0] != aliases[1]));
    ready = eventfd(0, EFD_SEMAPHORE); done = eventfd(0, EFD_SEMAPHORE);
    assert(ready >= 0 && done >= 0);
    pthread_t threads[2], memory;
    assert(!pthread_create(&memory, NULL, memory_worker, NULL));
    for (uintptr_t i = 0; i < 2; i++) assert(!pthread_create(threads + i, NULL, waiter, (void *)i));
    while (!atomic_load_explicit(&active, memory_order_acquire)) assert(!sched_yield());
    for (int round = 0; round < ROUNDS; round++) {
        atomic_store_explicit(&gate, round + 1, memory_order_release);
        assert(futex(&gate, FUTEX_WAKE, 2, 0, NULL, 0) >= 0);
        event(ready, 0); event(ready, 0);
        errno = 0;
        assert(futex(aliases[1], FUTEX_CMP_REQUEUE, 1, 1, aliases[0] + 1, 1) == -1 && errno == EAGAIN);
        unsigned moved = 0;
        for (unsigned tries = 0; moved < 2 && tries < 100000; tries++) {
            long n = futex(aliases[1], FUTEX_CMP_REQUEUE, 0, 2 - moved, aliases[0] + 1, 0);
            assert(n >= 0 && n <= 2 - moved); moved += (unsigned)n;
            if (moved < 2) assert(!sched_yield());
        }
        assert(moved == 2);
        assert(futex(aliases[0], FUTEX_WAKE, 2, 0, NULL, 0) == 0);
        // Both tickets already exist: one atomic operation wakes the first and
        // moves the other back. Address aliases and its original bitset survive.
        assert(futex(aliases[1] + 1, FUTEX_REQUEUE, 1, 1, aliases[0], 0) == 2);
        assert(futex(aliases[0] + 1, FUTEX_WAKE, 2, 0, NULL, 0) == 0);
        long one = futex(aliases[1], FUTEX_WAKE_BITSET, 1, 0, NULL, 1);
        assert(one == 0 || one == 1);
        assert(futex(aliases[1], FUTEX_WAKE_BITSET, 1, 0, NULL, 2) == 1 - one);
        event(done, 0); event(done, 0);
    }
    for (unsigned i = 0; i < 2; i++) assert(!pthread_join(threads[i], NULL));
    atomic_store_explicit(&stop, 1, memory_order_release);
    assert(!pthread_join(memory, NULL));
    if (shared) assert(!munmap(aliases[1], 4096));
    assert(!munmap(aliases[0], 4096));
    assert(!close(fd) && !close(ready) && !close(done));
    printf("worker-futex %s: %d rounds, alias keys, compare rollback, atomic wake/requeue, bitsets: passed\n", argv[1], ROUNDS);
    return 0;
}
