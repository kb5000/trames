// SPDX-License-Identifier: GPL-3.0-or-later
// The same persistent PTY exchange runs natively and inside the guest.
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <pty.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <termios.h>
#include <unistd.h>

#ifdef TRAMES_HOST_ORACLE
static void *native_fill(void *p, int c, size_t n) { return memset(p, c, n); }
#else
extern void *native_fill(void *, int, size_t);
#endif
enum { ROUNDS = 64, SIZE = 32 };
static int ends[2], pollers[2], mode;
static unsigned long sums[2];
static void ready(unsigned id) {
    if (mode == 2) {
        struct epoll_event event;
        assert(epoll_wait(pollers[id], &event, 1, 10000) == 1);
        assert(event.events & EPOLLIN); assert(event.data.u64 == id + 42);
    } else if (mode == 3) {
        struct pollfd fd = {.fd = ends[id], .events = POLLIN};
        struct timespec timeout = {.tv_sec = 10};
        assert(ppoll(&fd, 1, &timeout, NULL) == 1 && (fd.revents & POLLIN));
    }
}
static void receive(unsigned id, unsigned round) {
    char bytes[SIZE] = {0};
    ready(id);
    size_t done = 0;
    while (done < SIZE) {
        ssize_t n;
        if (mode == 1 && !done) {
            struct iovec parts[] = {{bytes, 5}, {bytes + 5, SIZE - 5}};
            n = readv(ends[id], parts, 2);
        } else n = read(ends[id], bytes + done, SIZE - done);
        assert(n > 0); done += (size_t)n;
    }
    char expected = (char)('a' + (round + 1 - id) % 26);
    for (unsigned i = 0; i < SIZE; i++) { assert(bytes[i] == expected); sums[id] += (unsigned char)bytes[i]; }
}
static void send_bytes(unsigned id, unsigned round) {
    char bytes[8192]; char value = (char)('a' + (round + id) % 26);
    assert(native_fill(bytes, value, sizeof bytes) == bytes);
    assert(bytes[0] == value && bytes[8191] == value);
    if (mode == 1) {
        struct iovec parts[] = {{bytes, 7}, {bytes + 7, SIZE - 7}};
        assert(writev(ends[id], parts, 2) == SIZE);
    } else assert(write(ends[id], bytes, SIZE) == SIZE);
}
static void *slave(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < ROUNDS; i++) { receive(1, i); send_bytes(1, i); }
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    const char *modes[] = {"read", "vector", "epoll", "poll"};
    for (mode = 0; mode < 4 && strcmp(argv[1], modes[mode]); mode++) {}
    assert(mode < 4);
    if (!strcmp(argv[2], "pinned")) {
        cpu_set_t cpus; CPU_ZERO(&cpus);
#ifdef TRAMES_HOST_ORACLE
        assert(!sched_getaffinity(0, sizeof cpus, &cpus));
        int cpu; for (cpu = 0; cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &cpus); cpu++) {}
        assert(cpu < CPU_SETSIZE); CPU_ZERO(&cpus); CPU_SET(cpu, &cpus);
#else
        CPU_SET(0, &cpus);
#endif
        assert(!sched_setaffinity(0, sizeof cpus, &cpus));
    } else assert(!strcmp(argv[2], "spread"));
    assert(!openpty(ends, ends + 1, NULL, NULL, NULL));
    struct termios attr; assert(!tcgetattr(ends[1], &attr)); cfmakeraw(&attr);
    assert(!tcsetattr(ends[1], TCSANOW, &attr));
    struct winsize size = {.ws_row = 37, .ws_col = 123}, out = {0};
    assert(!ioctl(ends[1], TIOCSWINSZ, &size));
    assert(!ioctl(ends[0], TIOCGWINSZ, &out) && out.ws_row == 37 && out.ws_col == 123);
    if (mode == 2) for (unsigned id = 0; id < 2; id++) {
        pollers[id] = epoll_create1(EPOLL_CLOEXEC); assert(pollers[id] >= 0);
        struct epoll_event event = {.events = EPOLLIN | EPOLLET, .data.u64 = id + 42};
        assert(!epoll_ctl(pollers[id], EPOLL_CTL_ADD, ends[id], &event));
    }
    pthread_t thread; assert(!pthread_create(&thread, NULL, slave, NULL));
    for (unsigned i = 0; i < ROUNDS; i++) { send_bytes(0, i); receive(0, i); }
    assert(!pthread_join(thread, NULL));
    assert(!close(ends[1]));
    struct pollfd hungup = {.fd = ends[0], .events = POLLIN};
    assert(poll(&hungup, 1, 0) == 1 && (hungup.revents & POLLHUP));
    char byte; assert(read(ends[0], &byte, 1) == -1 && errno == EIO);
    assert(!close(ends[0]));
    if (mode == 2) for (unsigned id = 0; id < 2; id++) assert(!close(pollers[id]));
    printf("worker-pty %s %s %u %lu\n", argv[1], argv[2], ROUNDS, sums[0] + sums[1]);
    return 0;
}
