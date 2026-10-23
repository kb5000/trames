// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { ROUNDS = 128 };
static _Atomic unsigned ready, go, done, stop;
static unsigned previous[2 * ROUNDS];
static void *peer(void *unused) {
    (void)unused;
    atomic_store_explicit(&ready, 1, memory_order_release);
    while (!atomic_load_explicit(&go, memory_order_acquire)) sched_yield();
    for (unsigned i = 0; i < ROUNDS; i++) previous[i] = umask(i + 1);
    atomic_store_explicit(&done, 1, memory_order_release);
    while (!atomic_load_explicit(&stop, memory_order_acquire)) sched_yield();
    return NULL;
}
static double now(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void check_cwd(const char *expected) {
    char path[4096]; assert(getcwd(path, sizeof path));
    if (strcmp(path, expected)) fprintf(stderr, "cwd=%s expected=%s\n", path, expected);
    assert(!strcmp(path, expected));
}
struct Child { const char *self, *directory; unsigned mode; };
static int child(void *argument) {
    struct Child *c = argument;
    if (c->mode == 2) {
        execl(c->self, c->self, "verify-exec", c->directory, (char *)NULL);
        assert(0);
    }
    if (c->mode == 1) {
        execl("/missing-fs-context-executable", "missing", (char *)NULL);
        assert(errno == ENOENT);
    }
    assert(umask(0077) == 0022);
    assert(!chdir(c->directory));
    return 0;
}
static void launch(unsigned flags, struct Child *c) {
    size_t size = 1 << 20; char *stack = malloc(size); assert(stack);
    pid_t pid = clone(child, stack + size, flags | SIGCHLD, c); assert(pid > 0);
    int status; assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    free(stack);
}
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "verify-exec")) {
        assert(argc == 3);
        assert(umask(0077) == 0022);
        assert(!chdir(argv[2]));
        return 0;
    }
    assert(argc == 2 || (argc == 3 && !strcmp(argv[2], "benchmark")));
    assert(!mkdir(argv[1], 0700));
    struct Child c = {.self=argv[0], .directory=argv[1]};
    const unsigned flags[] = {0, CLONE_VM, CLONE_FS, CLONE_VM | CLONE_FS};
    for (unsigned i = 0; argc == 2 && i < sizeof flags / sizeof *flags; i++) {
        assert(!chdir("/")); umask(0022);
        launch(flags[i], &c);
        check_cwd(flags[i] & CLONE_FS ? argv[1] : "/");
        assert(umask(0022) == (flags[i] & CLONE_FS ? 0077 : 0022));
    }
    for (c.mode = 1; argc == 2 && c.mode <= 2; c.mode++) {
        assert(!chdir("/")); umask(0022);
        launch(CLONE_FS, &c);
        // Ordinary exec unshares Files, but retains a CLONE_FS context.
        check_cwd(argv[1]);
        assert(umask(0022) == 0077);
    }
    umask(0);
    pthread_t thread; assert(!pthread_create(&thread, NULL, peer, NULL));
    while (!atomic_load_explicit(&ready, memory_order_acquire)) sched_yield();
    double begin = now();
    atomic_store_explicit(&go, 1, memory_order_release);
    for (unsigned i = 0; i < ROUNDS; i++) previous[ROUNDS + i] = umask(ROUNDS + i + 1);
    while (!atomic_load_explicit(&done, memory_order_acquire)) sched_yield();
    double elapsed = now() - begin;
    unsigned seen[2 * ROUNDS + 1] = {0};
    unsigned final = umask(0022); assert(final <= 2 * ROUNDS); seen[final]++;
    for (unsigned i = 0; i < 2 * ROUNDS; i++) {
        assert(previous[i] <= 2 * ROUNDS); seen[previous[i]]++;
    }
    for (unsigned i = 0; i <= 2 * ROUNDS; i++) assert(seen[i] == 1);
    atomic_store_explicit(&stop, 1, memory_order_release);
    assert(!pthread_join(thread, NULL));
    assert(!chdir("/") && !rmdir(argv[1]));
    printf("shared-fs-context exchanges=%u seconds=%.9f PASS\n", 2 * ROUNDS, elapsed);
    return 0;
}
