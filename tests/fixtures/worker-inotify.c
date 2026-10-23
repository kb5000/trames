// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

enum { ROUNDS = 64 };
static int notify, epoll, watch;
static _Atomic unsigned started, acknowledged, release_peer;
static void *reader(void *unused) {
    (void)unused;
    atomic_store_explicit(&started, 1, memory_order_release);
    for (unsigned i = 0; i < ROUNDS; i++) {
        struct inotify_event event;
        if (i & 1) {
            struct epoll_event ready;
            assert(epoll_wait(epoll, &ready, 1, 2000) == 1);
            assert(ready.events == EPOLLIN && ready.data.u64 == 42);
            int available = -1;
            assert(!ioctl(notify, FIONREAD, &available) && available == 16);
            struct iovec vector[] = {{&event, sizeof event}, {NULL, 0}};
            assert(readv(notify, vector, 2) == 16);
        } else {
            assert(read(notify, &event, sizeof event) == 16);
        }
        assert(event.wd == watch && event.mask == IN_MODIFY && event.len == 0);
        atomic_store_explicit(&acknowledged, i + 1, memory_order_release);
    }
    while (!atomic_load_explicit(&release_peer, memory_order_acquire)) sched_yield();
    return NULL;
}
static double now(void) {
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    int file = open(argv[1], O_CREAT | O_TRUNC | O_RDWR, 0600);
    assert(file >= 0);
    char initial[4096] = {0};
    assert(write(file, initial, sizeof initial) == sizeof initial);
    notify = inotify_init1(IN_CLOEXEC);
    assert(notify >= 0);
    watch = inotify_add_watch(notify, argv[1], IN_MODIFY);
    assert(watch > 0);
    epoll = epoll_create1(0);
    assert(epoll >= 0);
    struct epoll_event interest = {.events = EPOLLIN | EPOLLET, .data.u64 = 42};
    assert(!epoll_ctl(epoll, EPOLL_CTL_ADD, notify, &interest));
    pthread_t peer;
    assert(!pthread_create(&peer, NULL, reader, NULL));
    while (!atomic_load_explicit(&started, memory_order_acquire)) sched_yield();
    double begin = now();
    for (unsigned i = 0; i < ROUNDS; i++) {
        char byte = (char)i;
        assert(pwrite(file, &byte, 1, i) == 1);
        while (atomic_load_explicit(&acknowledged, memory_order_acquire) != i + 1)
            sched_yield();
    }
    double elapsed = now() - begin;
    assert(!inotify_rm_watch(notify, watch));
    struct inotify_event ignored;
    assert(read(notify, &ignored, sizeof ignored) == 16);
    assert(ignored.wd == watch && ignored.mask == IN_IGNORED && ignored.len == 0);
    atomic_store_explicit(&release_peer, 1, memory_order_release);
    assert(!pthread_join(peer, NULL));
    assert(!close(file) && !close(notify) && !close(epoll));
    assert(!unlink(argv[1]));
    printf("worker-inotify rounds=%u seconds=%.9f PASS\n", ROUNDS, elapsed);
    return 0;
}
