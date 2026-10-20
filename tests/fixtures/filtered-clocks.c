// SPDX-License-Identifier: GPL-3.0-or-later
// Fixed syscall work, with an active inherited filter and four synchronized CPUs.
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static pthread_barrier_t barrier;
static int iterations, threads;
static long pid;
static void *queries(void *unused) {
    (void)unused;
    errno = 0;
    assert(syscall(SYS_getppid) == -1 && errno == EPERM);
    struct timespec last = {0};
    int r = pthread_barrier_wait(&barrier);
    assert(!r || r == PTHREAD_BARRIER_SERIAL_THREAD);
    for (int i = 0; i < iterations; ++i) {
        struct timespec now;
        assert(syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &now) == 0);
        assert(now.tv_nsec >= 0 && now.tv_nsec < 1000000000);
        assert(now.tv_sec > last.tv_sec || (now.tv_sec == last.tv_sec && now.tv_nsec >= last.tv_nsec));
        assert(syscall(SYS_getpid) == pid);
        last = now;
    }
    return NULL;
}
int main(int argc, char **argv) {
    iterations = argc > 1 ? atoi(argv[1]) : 2000;
    threads = argc > 2 ? atoi(argv[2]) : 4;
    assert(iterations > 0 && iterations <= 100000 && threads > 0 && threads <= 4);
    pid = getpid();
    struct sock_filter code[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_getppid, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog program = {.len = 4, .filter = code};
    assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
    assert(syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &program) == 0);
    assert(pthread_barrier_init(&barrier, NULL, (unsigned)threads) == 0);
    pthread_t workers[4];
    for (int i = 0; i < threads; ++i) assert(pthread_create(&workers[i], NULL, queries, NULL) == 0);
    for (int i = 0; i < threads; ++i) assert(pthread_join(workers[i], NULL) == 0);
    assert(pthread_barrier_destroy(&barrier) == 0);
    puts("filtered-clocks: denied call, monotonic clocks and identities: ok");
    return 0;
}
