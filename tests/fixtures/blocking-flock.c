// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
static int a, b, result, failure;
static _Atomic unsigned entered, done, handled, phase, ack, ready, stop;
static void await(_Atomic unsigned *p, unsigned value) {
    while (atomic_load_explicit(p, memory_order_acquire) != value) sched_yield();
}
static void delay(void) {
    struct timespec t = { .tv_nsec = 50000000 };
    while (nanosleep(&t, &t)) assert(errno == EINTR);
}
static void signal_handler(int sig) { (void)sig; atomic_fetch_add(&handled, 1); }
static void *waiter(void *unused) {
    (void)unused;
    atomic_store_explicit(&entered, 1, memory_order_release);
    result = flock(a, LOCK_EX); failure = errno;
    atomic_store_explicit(&done, 1, memory_order_release);
    return NULL;
}
static void open_pair(const char *path) {
    a = open(path, O_CREAT | O_RDWR, 0600); assert(a >= 0);
    b = open(path, O_RDWR); assert(b >= 0);
    entered = done = handled = 0;
    assert(!flock(b, LOCK_EX));
}
static void domains(const char *path) {
    open_pair(path);
    pthread_t t; assert(!pthread_create(&t, NULL, waiter, NULL));
    await(&entered, 1); delay(); assert(!atomic_load(&done));
    assert(!close(a));
    char reused[1024]; assert(snprintf(reused, sizeof(reused), "%s.reused", path) < (int)sizeof(reused));
    int other = open(reused, O_CREAT | O_RDWR, 0600); assert(other >= 0);
    if (other != a) { assert(dup2(other, a) == a); assert(!close(other)); }
    assert(!unlink(reused));
    assert(!flock(a, LOCK_EX));
    delay(); assert(!atomic_load(&done)); // Must still wait on the original inode.
    assert(!flock(b, LOCK_UN));
    assert(!pthread_join(t, NULL)); assert(result == 0);
    assert(!flock(b, LOCK_EX | LOCK_NB)); // No original FD survived completion.
    assert(!close(a)); assert(!close(b));
    for (unsigned restart = 0; restart < 2; restart++) {
        open_pair(path);
        struct sigaction action = { .sa_handler = signal_handler, .sa_flags = restart ? SA_RESTART : 0 };
        sigemptyset(&action.sa_mask); assert(!sigaction(SIGUSR1, &action, NULL));
        assert(!pthread_create(&t, NULL, waiter, NULL));
        await(&entered, 1); delay(); assert(!atomic_load(&done));
        assert(!pthread_kill(t, SIGUSR1)); await(&handled, 1);
        if (restart) {
            delay(); assert(!atomic_load(&done)); assert(!flock(b, LOCK_UN));
        }
        assert(!pthread_join(t, NULL));
        assert(restart ? result == 0 : (result == -1 && failure == EINTR));
        assert(!close(a)); assert(!close(b));
    }
}
enum { ROUNDS = 64 };
static void *peer(void *unused) {
    (void)unused; atomic_store_explicit(&ready, 1, memory_order_release);
    for (unsigned i = 1; i <= ROUNDS; i++) {
        await(&phase, i);
        atomic_store_explicit(&entered, i, memory_order_release);
        assert(!flock(b, LOCK_EX)); assert(!flock(b, LOCK_UN));
        atomic_store_explicit(&ack, i, memory_order_release);
    }
    await(&stop, 1); return NULL;
}
static double now(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
    assert(argc == 2 || (argc == 3 && !strcmp(argv[2], "benchmark")));
    alarm(20);
    if (argc == 2) domains(argv[1]);
    open_pair(argv[1]); assert(!flock(b, LOCK_UN));
    pthread_t t; assert(!pthread_create(&t, NULL, peer, NULL)); await(&ready, 1);
    double begin = now();
    for (unsigned i = 1; i <= ROUNDS; i++) {
        assert(!flock(a, LOCK_EX)); atomic_store_explicit(&phase, i, memory_order_release);
        await(&entered, i);
        // Yield to the announced blocking operation; no timed delay in benchmark.
        for (int j = 0; j < 4; j++) sched_yield();
        assert(!flock(a, LOCK_UN)); await(&ack, i);
    }
    double elapsed = now() - begin;
    atomic_store_explicit(&stop, 1, memory_order_release); assert(!pthread_join(t, NULL));
    assert(!close(a)); assert(!close(b));
    printf("blocking-flock calls=%u seconds=%.9f PASS\n", ROUNDS * 4, elapsed);
    return 0;
}
