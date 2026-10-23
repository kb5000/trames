// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/klog.h>
#include <sys/wait.h>
#include <unistd.h>

enum { ROUNDS = 8 };
static void post(int fd) { assert(!eventfd_write(fd, 1)); }
static void take(int fd) { eventfd_t value; assert(!eventfd_read(fd, &value) && value == 1); }
int main(int argc, char **argv) {
    int modes = argc == 2 && !strcmp(argv[1], "no-epoll") ? 3 : 4;
    int writer = open("/dev/kmsg", O_WRONLY);
    assert(writer >= 0);
    for (int mode = 0; mode < modes; ++mode) {
        char bytes[2048];
        while (klogctl(9, NULL, 0)) assert(klogctl(2, bytes, sizeof(bytes)) > 0);
        int reader = open("/dev/kmsg", O_RDONLY), ready = eventfd(0, 0), ack = eventfd(0, 0);
        assert(reader >= 0 && ready >= 0 && ack >= 0 && lseek(reader, 0, SEEK_END) == 0);
        int ep = epoll_create1(0); assert(ep >= 0);
        struct epoll_event event = {.events = EPOLLIN | EPOLLET, .data.u64 = 42};
        if (mode == 3) assert(!epoll_ctl(ep, EPOLL_CTL_ADD, reader, &event));
        pid_t child = fork(); assert(child >= 0);
        if (!child) {
            for (int round = 0; round < ROUNDS; ++round) {
                post(ready);
                if (mode == 2) {
                    struct pollfd p = {.fd = reader, .events = POLLIN};
                    assert(poll(&p, 1, 2000) == 1 && p.revents == POLLIN);
                } else if (mode == 3) {
                    assert(epoll_wait(ep, &event, 1, 2000) == 1 && event.data.u64 == 42 && event.events == EPOLLIN);
                }
                int n = mode == 0 ? klogctl(2, bytes, sizeof(bytes) - 1) : read(reader, bytes, sizeof(bytes) - 1);
                assert(n > 0); bytes[n] = 0;
                char expected[80]; snprintf(expected, sizeof(expected), "log-mode-%d-round-%d", mode, round);
                assert(strstr(bytes, expected));
                post(ack);
            }
            _exit(0);
        }
        for (int round = 0; round < ROUNDS; ++round) {
            take(ready);
            int n = snprintf(bytes, sizeof(bytes), "log-mode-%d-round-%d", mode, round);
            assert(write(writer, bytes, n) == n);
            take(ack);
        }
        int status; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
        assert(!close(reader) && !close(ready) && !close(ack) && !close(ep));
    }
    assert(!close(writer));
    printf("worker-log-wait modes=%d: legacy stream, kmsg, poll, edge epoll and fork cursors: ok\n", modes);
    return 0;
}
