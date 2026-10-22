// SPDX-License-Identifier: GPL-3.0-or-later
// Real musl waits/requeues with a native memory worker runnable in parallel.
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
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef TRAMES_HOST_ORACLE
static void *native_fill(void *p, int c, size_t n) { return memset(p, c, n); }
#else
extern void *native_fill(void *, int, size_t);
#endif
static _Atomic int stop, started;
static _Atomic int words[2];
static int handshake[2];
static void retry(void) {
    const struct timespec delay = {0, 1000000};
    assert(!nanosleep(&delay, NULL));
}
static void *memory_worker(void *unused) {
    (void)unused;
    unsigned char *p = malloc(65536); assert(p);
    unsigned i = 0;
    do {
        unsigned char c = (unsigned char)++i;
        assert(native_fill(p, c, 65536) == p);
        assert(p[0] == c && p[32768] == c && p[65535] == c);
        atomic_store_explicit(&started, 1, memory_order_release);
    } while (!atomic_load_explicit(&stop, memory_order_acquire));
    free(p);
    return NULL;
}
static void *waiter(void *arg) {
    uintptr_t bits = (uintptr_t)arg;
    assert(write(handshake[1], "r", 1) == 1);
    assert(syscall(SYS_futex, words, FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG,
                   0, NULL, NULL, bits) == 0);
    return NULL;
}
static void wake_one(_Atomic int *word, unsigned flags, unsigned bits) {
    for (unsigned i = 0; i < 10000; i++) {
        long n = syscall(SYS_futex, word, FUTEX_WAKE_BITSET | flags, 1, NULL, NULL, bits);
        assert(n == 0 || n == 1);
        if (n == 1) return;
        retry();
    }
    assert(!"waiter never became eligible");
}
int main(void) {
    assert(!pipe(handshake));
    pthread_t memory;
    assert(!pthread_create(&memory, NULL, memory_worker, NULL));
    while (!atomic_load_explicit(&started, memory_order_acquire)) assert(!sched_yield());
    for (unsigned round = 0; round < 16; round++) {
        pthread_t threads[3];
        for (unsigned i = 0; i < 3; i++)
            assert(!pthread_create(&threads[i], NULL, waiter, (void *)(uintptr_t)(1u << i)));
        for (unsigned i = 0; i < 3; i++) {
            char c; assert(read(handshake[0], &c, 1) == 1 && c == 'r');
        }
        wake_one(words, FUTEX_PRIVATE_FLAG, 1);
        unsigned moved = 0;
        for (unsigned i = 0; moved < 2 && i < 10000; i++) {
            long n = syscall(SYS_futex, words, FUTEX_CMP_REQUEUE | FUTEX_PRIVATE_FLAG,
                             0, 2 - moved, words + 1, 0);
            assert(n >= 0 && n <= 2 - moved);
            moved += (unsigned)n;
            if (moved < 2) retry();
        }
        assert(moved == 2);
        assert(syscall(SYS_futex, words, FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
                       3, NULL, NULL, 0) == 0);
        wake_one(words + 1, FUTEX_PRIVATE_FLAG, 2);
        wake_one(words + 1, FUTEX_PRIVATE_FLAG, 4);
        for (unsigned i = 0; i < 3; i++) assert(!pthread_join(threads[i], NULL));
        const struct timespec zero = {0, 0};
        errno = 0;
        assert(syscall(SYS_futex, words, FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
                       1, NULL, NULL, 0) == -1 && errno == EAGAIN);
        errno = 0;
        assert(syscall(SYS_futex, words, FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
                       0, &zero, NULL, 0) == -1 && errno == ETIMEDOUT);
        errno = 0;
        assert(syscall(SYS_futex, words, FUTEX_WAKE_BITSET | FUTEX_PRIVATE_FLAG,
                       1, NULL, NULL, 0) == -1 && errno == EINVAL);
    }
    atomic_store_explicit(&stop, 1, memory_order_release);
    assert(!pthread_join(memory, NULL));
    _Atomic int *shared = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(shared != MAP_FAILED);
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        assert(syscall(SYS_futex, shared, FUTEX_WAIT, 0, NULL, NULL, 0) == 0);
        _exit(0);
    }
    wake_one(shared, 0, FUTEX_BITSET_MATCH_ANY);
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(!munmap(shared, 4096));
    assert(!close(handshake[0]) && !close(handshake[1]));
    puts("shared-futex: private bitsets/requeue, timeout/validation, shared fork: passed");
    return 0;
}
