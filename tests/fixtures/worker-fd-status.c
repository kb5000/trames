// SPDX-License-Identifier: GPL-3.0-or-later
// Live OFD status across aliases while a second guest thread remains runnable.
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static _Atomic int active, stop;
static void *peer(void *unused) {
    (void)unused;
    atomic_store_explicit(&active, 1, memory_order_release);
    while (!atomic_load_explicit(&stop, memory_order_acquire)) {}
    return NULL;
}
static double now(void) {
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(void) {
    int fds[2]; assert(!pipe2(fds, O_CLOEXEC));
    int alias = dup(fds[0]); assert(alias >= 0);
    assert(fcntl(alias, F_GETFD) == 0);
    assert(fcntl(fds[0], F_GETFD) == FD_CLOEXEC);
    pthread_t thread; assert(!pthread_create(&thread, NULL, peer, NULL));
    while (!atomic_load_explicit(&active, memory_order_acquire)) assert(!sched_yield());
    const unsigned rounds = 256;
    const double start = now();
    for (unsigned i = 0; i < rounds; i++) {
        assert(!fcntl(alias, F_SETFL, O_NONBLOCK | O_APPEND));
        assert(fcntl(fds[0], F_GETFL) == (O_NONBLOCK | O_APPEND));
        int value = 0;
        assert(!ioctl(fds[0], FIONBIO, &value));
        assert(fcntl(alias, F_GETFL) == O_APPEND);
        value = 1;
        assert(!ioctl(alias, FIONBIO, &value));
        char byte;
        assert(read(fds[0], &byte, 1) == -1 && errno == EAGAIN);
    }
    const double elapsed = now() - start;
    atomic_store_explicit(&stop, 1, memory_order_release);
    assert(!pthread_join(thread, NULL));
    // Existing descriptor membership and last-close semantics still apply.
    assert(!close(fds[1]));
    char byte; assert(read(alias, &byte, 1) == 0);
    assert(!close(alias) && !close(fds[0]));
    printf("expected-worker-fd: %u\nfd-loop-seconds: %.9f\nworker-fd-status PASS\n", rounds * 5, elapsed);
    return 0;
}
