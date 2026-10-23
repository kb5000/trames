// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

enum { PEERS = 8, ALIASES = 96, ROUNDS = 256 };
static int channel[2];
static _Atomic unsigned entered;

static void *reader(void *unused) {
    (void)unused;
    atomic_fetch_add_explicit(&entered, 1, memory_order_release);
    char byte = 0;
    assert(read(channel[0], &byte, 1) == 1 && byte == 'x');
    return NULL;
}
static double now(void) {
    struct timespec time;
    assert(!clock_gettime(CLOCK_MONOTONIC, &time));
    return time.tv_sec + time.tv_nsec * 1e-9;
}
int main(void) {
    assert(!pipe(channel));
    int aliases[ALIASES];
    for (unsigned i = 0; i < ALIASES; ++i) {
        aliases[i] = dup(channel[1]);
        assert(aliases[i] >= 0 && aliases[i] < 160);
    }
    pthread_t peers[PEERS];
    for (unsigned i = 0; i < PEERS; ++i)
        assert(!pthread_create(&peers[i], NULL, reader, NULL));
    while (atomic_load_explicit(&entered, memory_order_acquire) != PEERS)
        sched_yield();

    double started = now();
    for (unsigned i = 0; i < ROUNDS; ++i) {
        int fd = fcntl(channel[1], F_DUPFD_CLOEXEC, 160);
        assert(fd == 160);
        assert(dup3(fd, 200, 0) == 200);
        assert(!close(fd));
        assert(!close(200));
    }
    double elapsed = now() - started;
    for (unsigned i = 0; i < ALIASES; ++i)
        assert(!close(aliases[i]));
    assert(write(channel[1], "xxxxxxxx", PEERS) == PEERS);
    for (unsigned i = 0; i < PEERS; ++i)
        assert(!pthread_join(peers[i], NULL));
    assert(!close(channel[1]));
    char byte;
    assert(read(channel[0], &byte, 1) == 0);
    assert(!close(channel[0]));
    printf("shared-files-table peers=%u aliases=%u mutations=%u seconds=%.9f PASS\n",
           PEERS, ALIASES, ROUNDS * 4, elapsed);
    return 0;
}
