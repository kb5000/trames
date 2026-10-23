// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
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

enum { ROUNDS = 256 };
static _Atomic unsigned ready, go, done, stop;
static uintptr_t expected;
static uintptr_t brk_raw(uintptr_t value) { return (uintptr_t)syscall(SYS_brk, value); }
static void check(void) { assert(brk_raw(0) == expected); }
static void *peer(void *unused) {
    (void)unused;
    atomic_store_explicit(&ready, 1, memory_order_release);
    while (!atomic_load_explicit(&go, memory_order_acquire)) sched_yield();
    for (unsigned i = 0; i < ROUNDS; i++) check();
    atomic_store_explicit(&done, 1, memory_order_release);
    while (!atomic_load_explicit(&stop, memory_order_acquire)) sched_yield();
    return NULL;
}
struct Child { const char *self; uintptr_t base; unsigned mode; };
static int child(void *argument) {
    struct Child *c = argument;
    if (c->mode == 1) {
        execl(c->self, c->self, "verify-exec", (char *)NULL); assert(0);
    }
    if (c->mode == 2) {
        execl("/missing-vm-executable", "missing", (char *)NULL);
        assert(errno == ENOENT);
    }
    assert(brk_raw(0) == c->base);
    assert(brk_raw(c->base + 4096) == c->base + 4096);
    return 0;
}
static void launch(unsigned flags, struct Child *c, void *stack) {
    pid_t pid = clone(child, (char *)stack + (1 << 20), flags | SIGCHLD, c); assert(pid > 0);
    int status; assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static double now(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "verify-exec")) {
        uintptr_t base = brk_raw(0);
        assert(brk_raw(base + 8192) == base + 8192);
        return 0;
    }
    assert(argc == 1 || (argc == 2 && !strcmp(argv[1], "benchmark")));
    if (argc == 1) {
        // Keep clone stacks away from libc's heap while raw brk is exercised.
        void *stack = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(stack != MAP_FAILED);
        struct Child c = {.self=argv[0], .base=brk_raw(0)};
        const unsigned flags[] = {0, CLONE_FILES | CLONE_FS, CLONE_VM, CLONE_VM | CLONE_FILES | CLONE_FS};
        for (unsigned i = 0; i < sizeof flags / sizeof *flags; i++) {
            launch(flags[i], &c, stack);
            assert(brk_raw(0) == c.base + ((flags[i] & CLONE_VM) ? 4096 : 0));
            assert(brk_raw(c.base) == c.base);
        }
        for (c.mode = 1; c.mode <= 2; c.mode++) {
            launch(CLONE_VM, &c, stack);
            assert(brk_raw(0) == c.base + (c.mode == 2 ? 4096 : 0));
            assert(brk_raw(c.base) == c.base);
        }
        assert(!munmap(stack, 1 << 20));
    }
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, peer, NULL));
    while (!atomic_load_explicit(&ready, memory_order_acquire)) sched_yield();
    // pthread creation may allocate using libc brk; snapshot only afterwards.
    expected = brk_raw(0);
    double begin = now();
    atomic_store_explicit(&go, 1, memory_order_release);
    for (unsigned i = 0; i < ROUNDS; i++) check();
    while (!atomic_load_explicit(&done, memory_order_acquire)) sched_yield();
    double elapsed = now() - begin;
    atomic_store_explicit(&stop, 1, memory_order_release);
    assert(!pthread_join(thread, NULL));
    printf("shared-vm-allocation queries=%u seconds=%.9f PASS\n", 2 * ROUNDS, elapsed);
    return 0;
}
