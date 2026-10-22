// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifdef TRAMES_HOST_ORACLE
static void *native_fill(void *p, int c, size_t n) { return memset(p, c, n); }
#else
extern void *native_fill(void *, int, size_t);
#endif
static _Atomic int turn;
static unsigned char payload[4096];
static unsigned rounds = 2048;
static uint64_t sums[2];
static unsigned char value(unsigned i, unsigned id) { return (unsigned char)(i * 131 + id * 47 + 3); }
static void check(unsigned char expected) {
    assert(payload[0] == expected && payload[2047] == expected && payload[4095] == expected);
}
static void *exchange(void *arg) {
    unsigned id = (unsigned)(uintptr_t)arg;
    for (unsigned i = 0; i < rounds; i++) {
        while (atomic_load_explicit(&turn, memory_order_acquire) != (int)id) {
            long rc = syscall(SYS_futex, &turn, 128, 1 - id, NULL, NULL, 0);
            assert(rc == 0 || (rc == -1 && (errno == EAGAIN || errno == EINTR)));
        }
        if (id || i) {
            unsigned char expected = value(id ? i : i - 1, 1 - id);
            check(expected);
            sums[id] += expected;
        }
        assert(native_fill(payload, value(i, id), sizeof payload) == payload);
        atomic_store_explicit(&turn, 1 - id, memory_order_release);
        assert(syscall(SYS_futex, &turn, 129, 1, NULL, NULL, 0) >= 0);
    }
    return NULL;
}
int main(int argc, char **argv) {
    if (argc > 1) { rounds = (unsigned)strtoul(argv[1], NULL, 10); assert(rounds && rounds <= 100000); }
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, exchange, (void *)(uintptr_t)1));
    exchange(NULL);
    assert(!pthread_join(thread, NULL));
    check(value(rounds - 1, 1));
    sums[0] += value(rounds - 1, 1);
    printf("task-handoff %u %llu\n", rounds, (unsigned long long)(sums[0] + sums[1]));
    return 0;
}
