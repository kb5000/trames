// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#ifdef TRAMES_HOST_ORACLE
static void *native_fill(void *p, int c, size_t n) { return memset(p, c, n); }
#else
extern void *native_fill(void *, int, size_t);
#endif
static unsigned rounds = 1024;
static int mode, input[2], output[2], poller[2];
static unsigned char payload[4096];
static uint64_t sums[2];
static unsigned char value(unsigned i, unsigned id) { return (unsigned char)(i * 131 + id * 47 + 3); }
static void receive(unsigned id) {
    if (mode == 2) {
        struct epoll_event event;
        int rc;
        do { rc = epoll_wait(poller[id], &event, 1, -1); } while (rc == -1 && errno == EINTR);
        assert(rc == 1 && event.events & EPOLLIN && event.data.u64 == id + 42);
    }
    uint64_t token = 0;
    ssize_t rc;
    do { rc = read(input[id], &token, mode == 1 ? 8 : 1); } while (rc == -1 && errno == EINTR);
    assert(rc == (mode == 1 ? 8 : 1) && token == 1);
}
static void send_token(unsigned id) {
    uint64_t token = 1;
    assert(write(output[id], &token, mode == 1 ? 8 : 1) == (mode == 1 ? 8 : 1));
}
static void *exchange(void *arg) {
    unsigned id = (unsigned)(uintptr_t)arg;
    for (unsigned i = 0; i < rounds; i++) {
        receive(id);
        if (id || i) {
            unsigned char expected = value(id ? i : i - 1, 1 - id);
            assert(payload[0] == expected && payload[2047] == expected && payload[4095] == expected);
            sums[id] += expected;
        }
        assert(native_fill(payload, value(i, id), sizeof payload) == payload);
        send_token(1 - id);
    }
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc >= 2);
    mode = !strcmp(argv[1], "eventfd") ? 1 : !strcmp(argv[1], "epoll") ? 2 : 0;
    assert(mode || !strcmp(argv[1], "pipe"));
    if (argc > 2) { rounds = (unsigned)strtoul(argv[2], NULL, 10); assert(rounds && rounds <= 100000); }
    for (unsigned id = 0; id < 2; id++) {
        if (mode == 1) { input[id] = output[id] = eventfd(0, EFD_CLOEXEC); assert(input[id] >= 0); }
        else { int pair[2]; assert(!pipe(pair)); input[id] = pair[0]; output[id] = pair[1]; }
        if (mode == 2) {
            poller[id] = epoll_create1(EPOLL_CLOEXEC);
            assert(poller[id] >= 0);
            struct epoll_event event = { .events = EPOLLIN | EPOLLET, .data.u64 = id + 42 };
            assert(!epoll_ctl(poller[id], EPOLL_CTL_ADD, input[id], &event));
        }
    }
    send_token(0);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, exchange, (void *)(uintptr_t)1));
    exchange(NULL);
    assert(!pthread_join(thread, NULL));
    assert(payload[0] == value(rounds - 1, 1) && payload[4095] == value(rounds - 1, 1));
    sums[0] += value(rounds - 1, 1);
    for (unsigned id = 0; id < 2; id++) {
        assert(!close(input[id]));
        if (mode != 1) { assert(!close(output[id])); }
        if (mode == 2) { assert(!close(poller[id])); }
    }
    printf("io-handoff %s %u %llu\n", argv[1], rounds, (unsigned long long)(sums[0] + sums[1]));
    return 0;
}
