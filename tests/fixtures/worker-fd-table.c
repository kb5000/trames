// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { ROUNDS = 64, CLOEXEC = 4, UNSHARE = 2 };
static _Atomic unsigned phase, ack, active, stop;
static int shared_fd;
static void *observer(void *unused) {
    (void)unused;
    atomic_store_explicit(&active, 1, memory_order_release);
    for (unsigned i = 0; i < ROUNDS; i++) {
        while (atomic_load_explicit(&phase, memory_order_acquire) != i + 1) sched_yield();
        assert(fcntl(shared_fd, F_GETFD) == (int)(i & 1));
        atomic_store_explicit(&ack, i + 1, memory_order_release);
    }
    while (!atomic_load_explicit(&stop, memory_order_acquire)) sched_yield();
    return NULL;
}
static double now(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
struct Child { int fd, alias, mode; const char *self; };
static int child(void *arg) {
    struct Child *c = arg;
    if (c->mode == 1) {
        assert(!syscall(SYS_close_range, c->fd, c->fd, UNSHARE | CLOEXEC));
        assert(fcntl(c->fd, F_GETFD) == FD_CLOEXEC);
    } else if (c->mode == 2) {
        char fd[32], alias[32];
        snprintf(fd, sizeof fd, "%d", c->fd); snprintf(alias, sizeof alias, "%d", c->alias);
        execl(c->self, c->self, "verify-exec", fd, alias, (char *)NULL);
        assert(0);
    } else if (c->mode == 3) {
        execl("/this-fd-table-executable-does-not-exist", "missing", (char *)NULL);
        assert(errno == ENOENT);
        assert(!fcntl(c->fd, F_SETFD, FD_CLOEXEC));
    } else assert(!fcntl(c->fd, F_SETFD, FD_CLOEXEC));
    return 0;
}
static void launch(unsigned flags, struct Child *arg) {
    size_t size = 1 << 20; char *stack = malloc(size); assert(stack);
    pid_t pid = clone(child, stack + size, flags | SIGCHLD, arg); assert(pid > 0);
    int status; assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    free(stack);
}
static void domains(const char *self) {
    int p[2]; assert(!pipe(p)); int alias = dup(p[0]); assert(alias >= 0);
    struct Child c = {.fd=p[0], .alias=alias, .self=self};
    const unsigned flags[] = {0, CLONE_VM, CLONE_FILES, CLONE_VM | CLONE_FILES};
    for (unsigned i = 0; i < sizeof flags / sizeof *flags; i++) {
        assert(!fcntl(p[0], F_SETFD, 0));
        launch(flags[i], &c);
        assert(fcntl(p[0], F_GETFD) == ((flags[i] & CLONE_FILES) ? FD_CLOEXEC : 0));
        assert(fcntl(alias, F_GETFD) == 0);
    }
    assert(!fcntl(p[0], F_SETFD, 0)); c.mode = 1;
    launch(CLONE_FILES, &c); assert(fcntl(p[0], F_GETFD) == 0);
    c.mode = 2; assert(!fcntl(alias, F_SETFD, FD_CLOEXEC));
    launch(CLONE_FILES, &c);
    assert(fcntl(p[0], F_GETFD) == 0 && fcntl(alias, F_GETFD) == FD_CLOEXEC);
    c.mode = 3; launch(CLONE_FILES, &c);
    assert(fcntl(p[0], F_GETFD) == FD_CLOEXEC);
    assert(!close(p[0]) && !close(p[1]) && !close(alias));
    puts("worker-fd-table-domains PASS");
}
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "verify-exec")) {
        assert(argc == 4);
        int fd = atoi(argv[2]), alias = atoi(argv[3]);
        assert(fcntl(fd, F_GETFD) == 0);
        assert(fcntl(alias, F_GETFD) == -1 && errno == EBADF);
        assert(!fcntl(fd, F_SETFD, FD_CLOEXEC));
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "domains")) { domains(argv[0]); return 0; }
    assert(argc == 1);
    int p[2]; assert(!pipe(p)); shared_fd = p[0];
    int alias = fcntl(p[0], F_DUPFD, 32); assert(alias >= 32);
    pthread_t peer; assert(!pthread_create(&peer, NULL, observer, NULL));
    while (!atomic_load_explicit(&active, memory_order_acquire)) sched_yield();
    double start = now();
    for (unsigned i = 0; i < ROUNDS; i++) {
        assert(!fcntl(shared_fd, F_SETFD, i & 1));
        atomic_store_explicit(&phase, i + 1, memory_order_release);
        while (atomic_load_explicit(&ack, memory_order_acquire) != i + 1) sched_yield();
        assert(fcntl(shared_fd, F_GETFD) == (int)(i & 1));
        assert(fcntl(alias, F_GETFD) == 0);
        assert(!syscall(SYS_close_range, alias, UINT_MAX, CLOEXEC));
        assert(fcntl(alias, F_GETFD) == FD_CLOEXEC);
        assert(!fcntl(alias, F_SETFD, 0));
    }
    double elapsed = now() - start;
    atomic_store_explicit(&stop, 1, memory_order_release);
    assert(!pthread_join(peer, NULL));
    assert(!close(p[0]) && !close(p[1]) && !close(alias));
    printf("expected-worker-fd: %u\nfd-loop-seconds: %.9f\nworker-fd-table PASS\n", ROUNDS * 7, elapsed);
    return 0;
}
