// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
enum { ROUNDS = 64 };
static _Atomic unsigned phase, ack, ready, stop;
static int a, b;
static void await(_Atomic unsigned *word, unsigned value) {
    while (atomic_load_explicit(word, memory_order_acquire) != value) sched_yield();
}
static void rejected(int fd, int operation) {
    assert(flock(fd, operation | LOCK_NB) == -1 && errno == EWOULDBLOCK);
}
static void *peer(void *unused) {
    (void)unused;
    atomic_store_explicit(&ready, 1, memory_order_release);
    for (unsigned i = 0; i < ROUNDS; i++) {
        unsigned base = i * 4;
        await(&phase, base + 1);
        rejected(b, LOCK_SH); rejected(b, LOCK_EX);
        atomic_store_explicit(&ack, base + 1, memory_order_release);
        await(&phase, base + 2);
        assert(!flock(b, LOCK_SH | LOCK_NB));
        atomic_store_explicit(&ack, base + 2, memory_order_release);
        await(&phase, base + 4);
        assert(!flock(b, LOCK_UN));
        atomic_store_explicit(&ack, base + 4, memory_order_release);
    }
    await(&stop, 1);
    return NULL;
}
static void domains(const char *path) {
    int fd = open(path, O_CREAT | O_RDWR, 0600); assert(fd >= 0);
    int other = open(path, O_RDWR); assert(other >= 0);
    int alias = dup(fd); assert(alias >= 0);
    assert(!flock(fd, LOCK_EX | LOCK_NB));
    assert(!close(fd)); rejected(other, LOCK_SH);
    pid_t pid = fork(); assert(pid >= 0);
    if (!pid) { assert(!flock(alias, LOCK_UN)); _exit(0); }
    int status; assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status));
    assert(!flock(other, LOCK_SH | LOCK_NB));
    assert(!flock(alias, LOCK_SH | LOCK_NB));
    rejected(alias, LOCK_EX); // Failed conversion relinquishes alias's SH.
    assert(!flock(other, LOCK_EX | LOCK_NB));
    assert(!close(other)); assert(!close(alias));
    fd = open(path, O_PATH); assert(fd >= 0);
    assert(flock(fd, LOCK_EX | LOCK_NB) == -1 && errno == EBADF);
    assert(!close(fd));
}
static double now(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
    assert(argc == 2 || (argc == 3 && !strcmp(argv[2], "benchmark")));
    if (argc == 2) domains(argv[1]);
    a = open(argv[1], O_CREAT | O_RDWR, 0600); assert(a >= 0);
    b = open(argv[1], O_RDWR); assert(b >= 0);
    assert(!unlink(argv[1]));
    pthread_t thread; assert(!pthread_create(&thread, NULL, peer, NULL));
    await(&ready, 1);
    double start = now();
    for (unsigned i = 0; i < ROUNDS; i++) {
        unsigned base = i * 4;
        assert(!flock(a, LOCK_EX | LOCK_NB));
        atomic_store_explicit(&phase, base + 1, memory_order_release);
        await(&ack, base + 1);
        assert(!flock(a, LOCK_UN));
        atomic_store_explicit(&phase, base + 2, memory_order_release);
        await(&ack, base + 2);
        assert(!flock(a, LOCK_SH | LOCK_NB));
        rejected(a, LOCK_EX);
        atomic_store_explicit(&phase, base + 4, memory_order_release);
        await(&ack, base + 4);
        assert(!flock(a, LOCK_EX | LOCK_NB)); assert(!flock(a, LOCK_UN));
    }
    double elapsed = now() - start;
    atomic_store_explicit(&stop, 1, memory_order_release);
    assert(!pthread_join(thread, NULL));
    assert(!close(a)); assert(!close(b));
    printf("shared-flock calls=%u seconds=%.9f PASS\n", ROUNDS * 10, elapsed);
    return 0;
}
