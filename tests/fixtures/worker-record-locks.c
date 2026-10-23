// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { ROUNDS = 64 };
static _Atomic unsigned phase, ack, active, stop;
static int fd;
static pid_t parent_pid;
static int lock_op(int command, short kind, pid_t expect) {
    struct flock lock = {.l_type=kind, .l_whence=SEEK_SET, .l_start=4, .l_len=8};
    int rc = fcntl(fd, command, &lock);
    if (command == F_GETLK) {
        assert(rc == 0 && lock.l_type == F_WRLCK);
        assert(lock.l_whence == SEEK_SET && lock.l_start == 4 && lock.l_len == 8 && lock.l_pid == expect);
    }
    return rc;
}
static void rejected(void) {
    assert(lock_op(F_SETLK, F_WRLCK, 0) == -1 && (errno == EAGAIN || errno == EACCES));
}
static void await(_Atomic unsigned *word, unsigned value) {
    while (atomic_load_explicit(word, memory_order_acquire) != value) sched_yield();
}
static int peer(void *unused) {
    (void)unused;
    atomic_store_explicit(&active, 1, memory_order_release);
    for (unsigned i = 0; i < ROUNDS; i++) {
        unsigned base = i * 3;
        await(&phase, base + 1);
        assert(!lock_op(F_GETLK, F_WRLCK, parent_pid));
        rejected();
        assert(lock_op(F_SETLK, F_RDLCK, 0) == -1 && (errno == EAGAIN || errno == EACCES));
        atomic_store_explicit(&ack, base + 1, memory_order_release);
        await(&phase, base + 2);
        assert(!lock_op(F_SETLK, F_WRLCK, 0));
        atomic_store_explicit(&ack, base + 2, memory_order_release);
        await(&phase, base + 3);
        assert(!lock_op(F_SETLK, F_UNLCK, 0));
        atomic_store_explicit(&ack, base + 3, memory_order_release);
    }
    while (!atomic_load_explicit(&stop, memory_order_acquire)) sched_yield();
    return 0;
}
static double now(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    fd = open(argv[1], O_CREAT | O_TRUNC | O_RDWR, 0600); assert(fd >= 0);
    assert(!unlink(argv[1]));
    parent_pid = getpid();
    size_t size = 1 << 20; char *stack = malloc(size); assert(stack);
    // Same VM, distinct processes and file tables. POSIX locks use TGIDs,
    // unlike a pthread-only test where every participant has the same owner.
    pid_t child = clone(peer, stack + size, CLONE_VM | SIGCHLD, NULL); assert(child > 0);
    await(&active, 1);
    double start = now();
    for (unsigned i = 0; i < ROUNDS; i++) {
        unsigned base = i * 3;
        assert(!lock_op(F_SETLK, F_WRLCK, 0));
        atomic_store_explicit(&phase, base + 1, memory_order_release);
        await(&ack, base + 1);
        assert(!lock_op(F_SETLK, F_UNLCK, 0));
        atomic_store_explicit(&phase, base + 2, memory_order_release);
        await(&ack, base + 2);
        assert(!lock_op(F_GETLK, F_WRLCK, child));
        rejected();
        atomic_store_explicit(&phase, base + 3, memory_order_release);
        await(&ack, base + 3);
    }
    double elapsed = now() - start;
    atomic_store_explicit(&stop, 1, memory_order_release);
    int status; assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    free(stack);
    assert(!close(fd));
    printf("expected-worker-fd: %u\nfd-loop-seconds: %.9f\nworker-record-locks PASS\n", ROUNDS * 9, elapsed);
    return 0;
}
