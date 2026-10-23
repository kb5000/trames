// SPDX-License-Identifier: GPL-3.0-or-later
// Real established TCP with a runnable native-memory peer. No host FD shortcut.
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>
#ifdef TRAMES_HOST_ORACLE
static void *native_fill(void *p, int c, size_t n) { return memset(p, c, n); }
#else
extern void *native_fill(void *, int, size_t);
#endif
static _Atomic int active, stop;
static void *memory_peer(void *unused) {
    (void)unused;
    unsigned char data[4096]; unsigned i = 0;
    do {
        assert(native_fill(data, (int)++i, sizeof data) == data);
        assert(data[0] == (unsigned char)i && data[4095] == (unsigned char)i);
        atomic_store_explicit(&active, 1, memory_order_release);
    } while (!atomic_load_explicit(&stop, memory_order_acquire));
    return NULL;
}
static void wait_fd(int fd, short events) {
    struct pollfd p = {.fd = fd, .events = events};
    assert(poll(&p, 1, 10000) == 1 && (p.revents & (events | POLLHUP)));
}
int main(int argc, char **argv) {
    assert(argc == 4);
    int family = !strcmp(argv[1], "4") ? AF_INET : AF_INET6;
    assert(family == AF_INET || !strcmp(argv[1], "6"));
    int blocking = !strncmp(argv[2], "blocking-", 9);
    const char *mode = argv[2] + (blocking ? 9 : 0);
    int vector = !strcmp(mode, "vector"); assert(vector || !strcmp(mode, "write"));
    unsigned long port = strtoul(argv[3], NULL, 10); assert(port && port <= 65535);
    int fd = socket(family, SOCK_STREAM, 0); assert(fd >= 0);
    if (family == AF_INET) {
        struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
        assert(inet_pton(AF_INET, "127.0.0.1", &a.sin_addr) == 1);
        assert(!connect(fd, (void *)&a, sizeof a));
    } else {
        struct sockaddr_in6 a = {.sin6_family = AF_INET6, .sin6_port = htons((uint16_t)port)};
        assert(inet_pton(AF_INET6, "::1", &a.sin6_addr) == 1);
        assert(!connect(fd, (void *)&a, sizeof a));
    }
    if (blocking) {
        int size = 4096; assert(!setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof size));
        struct timeval timeout = {.tv_sec = 10};
        assert(!setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout));
    } else assert(!fcntl(fd, F_SETFL, O_NONBLOCK));
    pthread_t peer; assert(!pthread_create(&peer, NULL, memory_peer, NULL));
    while (!atomic_load_explicit(&active, memory_order_acquire)) assert(!sched_yield());
    for (unsigned round = 0; round < 128; round++) {
        char bytes[1024]; memset(bytes, (int)('a' + round % 26), sizeof bytes);
        size_t done = 0;
        while (done < sizeof bytes) {
            ssize_t n;
            if (vector && !done) {
                struct iovec parts[] = {{bytes, 17}, {bytes + 17, sizeof bytes - 17}};
                n = writev(fd, parts, 2);
            } else n = write(fd, bytes + done, sizeof bytes - done);
            if (n < 0) { assert(errno == EAGAIN || errno == EINTR); wait_fd(fd, POLLOUT); }
            else { assert(n > 0); done += (size_t)n; }
        }
    }
    assert(!shutdown(fd, SHUT_WR));
    char reply[5] = {0}; size_t done = 0;
    while (done < sizeof reply) {
        ssize_t n;
        if (vector && !done) {
            struct iovec parts[] = {{reply, 2}, {reply + 2, 3}};
            n = readv(fd, parts, 2);
        } else n = read(fd, reply + done, sizeof reply - done);
        if (n < 0) { assert(errno == EAGAIN || errno == EINTR); wait_fd(fd, POLLIN); }
        else { assert(n > 0); done += (size_t)n; }
    }
    assert(!memcmp(reply, "PASS\n", sizeof reply));
    if (blocking) { char byte; assert(read(fd, &byte, 1) == 0); }
    atomic_store_explicit(&stop, 1, memory_order_release);
    assert(!pthread_join(peer, NULL)); assert(!close(fd));
    printf("worker-tcp %s %s 128 131072\n", argv[1], argv[2]);
    return 0;
}
