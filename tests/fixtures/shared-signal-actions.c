// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { ROUNDS = 256 };
static volatile sig_atomic_t fired;
static _Atomic unsigned ready, go, done, stop;
static void handler(int signal) { assert(signal == SIGUSR1); fired++; }
static void install(int signal, void (*function)(int), int flags) {
    struct sigaction action = {.sa_handler=function, .sa_flags=flags};
    assert(!sigemptyset(&action.sa_mask));
    assert(!sigaddset(&action.sa_mask, SIGUSR2));
    assert(!sigaction(signal, &action, NULL));
}
static void check(void (*expected)(int)) {
    struct sigaction action; assert(!sigaction(SIGUSR1, NULL, &action));
    assert(action.sa_handler == expected);
    if (expected == handler) {
        assert(action.sa_flags & SA_RESTART);
        assert(sigismember(&action.sa_mask, SIGUSR2));
    }
}
static void *peer(void *unused) {
    (void)unused;
    atomic_store_explicit(&ready, 1, memory_order_release);
    while (!atomic_load_explicit(&go, memory_order_acquire)) sched_yield();
    for (unsigned i = 0; i < ROUNDS; i++) check(handler);
    atomic_store_explicit(&done, 1, memory_order_release);
    while (!atomic_load_explicit(&stop, memory_order_acquire)) sched_yield();
    return NULL;
}
struct Child { const char *self; unsigned mode; };
static int child(void *argument) {
    struct Child *c = argument;
    if (c->mode == 1) {
        // A raw CLONE_VM child is not a pthread and shares the parent's musl
        // TLS. Avoid raise()'s cached TID when targeting this child's signal.
        fired = 0;
        assert(!syscall(SYS_tgkill, syscall(SYS_getpid), syscall(SYS_gettid), SIGUSR1));
        assert(fired == 1); check(SIG_DFL);
        return 0;
    }
    if (c->mode == 2) {
        execl(c->self, c->self, "verify-exec", (char *)NULL); assert(0);
    }
    if (c->mode == 3) {
        execl("/missing-sighand-executable", "missing", (char *)NULL);
        assert(errno == ENOENT);
    }
    install(SIGUSR1, SIG_IGN, 0);
    return 0;
}
static void launch(unsigned flags, struct Child *c) {
    size_t size = 1 << 20; char *stack = malloc(size); assert(stack);
    pid_t pid = clone(child, stack + size, flags | SIGCHLD, c); assert(pid > 0);
    int status; assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    free(stack);
}
static double now(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "verify-exec")) {
        check(SIG_DFL);
        struct sigaction action; assert(!sigaction(SIGUSR2, NULL, &action));
        assert(action.sa_handler == SIG_IGN);
        install(SIGUSR1, SIG_IGN, 0);
        return 0;
    }
    assert(argc == 1 || (argc == 2 && !strcmp(argv[1], "benchmark")));
    if (argc == 1) {
        struct Child c = {.self=argv[0]};
        const unsigned flags[] = {0, CLONE_VM, CLONE_VM | CLONE_SIGHAND};
        for (unsigned i = 0; i < sizeof flags / sizeof *flags; i++) {
            install(SIGUSR1, handler, SA_RESTART);
            c.mode = 0; launch(flags[i], &c);
            check(flags[i] & CLONE_SIGHAND ? SIG_IGN : handler);
            install(SIGUSR1, handler, SA_RESTART | SA_RESETHAND);
            c.mode = 1; launch(flags[i], &c);
            check(flags[i] & CLONE_SIGHAND ? SIG_DFL : handler);
        }
        install(SIGUSR2, SIG_IGN, 0);
        for (c.mode = 2; c.mode <= 3; c.mode++) {
            install(SIGUSR1, handler, SA_RESTART);
            launch(CLONE_VM | CLONE_SIGHAND, &c);
            check(c.mode == 2 ? handler : SIG_IGN);
        }
    }
    install(SIGUSR1, handler, SA_RESTART);
    pthread_t thread; assert(!pthread_create(&thread, NULL, peer, NULL));
    while (!atomic_load_explicit(&ready, memory_order_acquire)) sched_yield();
    double begin = now();
    atomic_store_explicit(&go, 1, memory_order_release);
    for (unsigned i = 0; i < ROUNDS; i++) check(handler);
    while (!atomic_load_explicit(&done, memory_order_acquire)) sched_yield();
    double elapsed = now() - begin;
    atomic_store_explicit(&stop, 1, memory_order_release);
    assert(!pthread_join(thread, NULL));
    printf("shared-signal-actions queries=%u seconds=%.9f PASS\n", 2 * ROUNDS, elapsed);
    return 0;
}
