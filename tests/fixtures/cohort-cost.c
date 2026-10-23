// SPDX-License-Identifier: GPL-3.0-or-later
// Fixed work to separate periodic cohort rebuilds from FD/VM checkpoints.
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static pthread_barrier_t gate;
static unsigned threads, rounds;
static int mode, descriptor;
static uint64_t results[32];

static uint64_t calculate(uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        value ^= value << 13;
        value ^= value >> 7;
        value ^= value << 17;
    }
    return value;
}
static void *worker(void *argument) {
    unsigned index = (unsigned)(uintptr_t)argument;
    int rc = pthread_barrier_wait(&gate);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    uint64_t value = index + 1;
    for (unsigned r = 0; r < rounds; ++r) {
        value = calculate(value, 4096);
        if (mode == 1) {
            int copy = dup(descriptor);
            assert(copy >= 0 && close(copy) == 0);
        } else if (mode == 2) {
            void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            assert(p != MAP_FAILED);
            *(volatile uint64_t *)p = value;
            assert(*(volatile uint64_t *)p == value && munmap(p, 4096) == 0);
        }
    }
    results[index] = value;
    rc = pthread_barrier_wait(&gate);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 5);
    mode = !strcmp(argv[1], "fd") ? 1 : !strcmp(argv[1], "vm") ? 2 : 0;
    assert(mode || !strcmp(argv[1], "compute"));
    threads = (unsigned)atoi(argv[2]);
    unsigned count = (unsigned)atoi(argv[3]);
    unsigned total = (unsigned)atoi(argv[4]);
    assert(threads >= 2 && threads <= 32 && count <= 256 && total >= threads);
    rounds = total / threads;
    int files[256];
    // Independent OFDs exercise real per-epoch preparation, not dup aliases.
    for (unsigned i = 0; i < count; ++i) {
        files[i] = open("cohort-data", O_CREAT | O_RDWR, 0600);
        assert(files[i] >= 0);
    }
    descriptor = count ? files[0] : STDOUT_FILENO;
    assert(pthread_barrier_init(&gate, NULL, threads) == 0);
    pthread_t workers[32];
    for (unsigned i = 1; i < threads; ++i)
        assert(pthread_create(&workers[i], NULL, worker, (void *)(uintptr_t)i) == 0);
    worker(NULL);
    for (unsigned i = 1; i < threads; ++i)
        assert(pthread_join(workers[i], NULL) == 0);
    assert(pthread_barrier_destroy(&gate) == 0);
    for (unsigned i = 0; i < count; ++i) assert(close(files[i]) == 0);
    // Do not re-run the arithmetic serially inside the timed guest. A native
    // oracle compares every result; the same total work is used in both builds.
    for (unsigned i = 0; i < threads; ++i)
        printf("%u:%016llx\n", i, (unsigned long long)results[i]);
    return 0;
}
