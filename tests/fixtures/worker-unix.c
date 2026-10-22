// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#ifdef TRAMES_HOST_ORACLE
static void *native_fill(void *p, int c, size_t n) { return memset(p, c, n); }
#else
extern void *native_fill(void *, int, size_t);
#endif
static unsigned rounds = 128;
static int mode, ends[2], poller[2], rights_pipe[2];
static unsigned char payload[4096];
static uint64_t sums[2];
static unsigned char value(unsigned i, unsigned id) { return (unsigned char)(i * 131 + id * 47 + 3); }
static void receive(unsigned id) {
    if (mode == 4) {
        struct epoll_event event;
        int rc;
        do { rc = epoll_wait(poller[id], &event, 1, -1); } while (rc == -1 && errno == EINTR);
        assert(rc == 1 && event.events & EPOLLIN && event.data.u64 == id + 42);
    }
    int available = -1;
    assert(!ioctl(ends[id], FIONREAD, &available) && available >= 0 && available <= 8);
    uint64_t token = 0;
    struct iovec vectors[] = {{&token, 3}, {(char *)&token + 3, 5}};
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int))]; } control = {0};
    struct msghdr msg = {.msg_iov = vectors, .msg_iovlen = 2};
    if (mode == 6) { msg.msg_control = control.bytes; msg.msg_controllen = sizeof control; }
    ssize_t rc;
    do {
        if (mode == 0 || mode == 4) rc = read(ends[id], &token, 8);
        else if (mode == 1) rc = readv(ends[id], vectors, 2);
        else if (mode == 3 || mode == 6) rc = recvmsg(ends[id], &msg, 0);
        else rc = recv(ends[id], &token, 8, mode == 5 ? MSG_WAITALL : 0);
    } while (rc == -1 && errno == EINTR);
    assert(rc == 8 && token == 1);
    if (mode == 6) {
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        assert(cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS);
        assert(cmsg->cmsg_len == CMSG_LEN(sizeof(int)) && !(msg.msg_flags & MSG_CTRUNC));
        int fd;
        memcpy(&fd, CMSG_DATA(cmsg), sizeof fd);
        assert(write(fd, "x", 1) == 1);
        assert(!close(fd));
        char ch = 0;
        assert(read(rights_pipe[0], &ch, 1) == 1 && ch == 'x');
    }
}
static void send_token(unsigned id) {
    uint64_t token = 1;
    struct iovec vectors[] = {{&token, 3}, {(char *)&token + 3, 5}};
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int))]; } control = {0};
    struct msghdr msg = {.msg_iov = vectors, .msg_iovlen = 2};
    if (mode == 6) {
        msg.msg_control = control.bytes; msg.msg_controllen = sizeof control;
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET; cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cmsg), &rights_pipe[1], sizeof(int));
    }
    ssize_t rc;
    if (mode == 0 || mode == 4) rc = write(ends[id], &token, 8);
    else if (mode == 1) rc = writev(ends[id], vectors, 2);
    else if (mode == 3 || mode == 6) rc = sendmsg(ends[id], &msg, MSG_NOSIGNAL);
    else if (mode == 5) {
        assert(send(ends[id], &token, 4, MSG_NOSIGNAL) == 4);
        assert(!sched_yield());
        rc = send(ends[id], (char *)&token + 4, 4, MSG_NOSIGNAL) + 4;
    } else rc = send(ends[id], &token, 8, MSG_NOSIGNAL);
    assert(rc == 8);
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
        send_token(id);
    }
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 4);
    const char *modes[] = {"read", "vector", "socket", "message", "epoll", "waitall", "rights"};
    for (mode = 0; mode < 7 && strcmp(argv[1], modes[mode]); mode++) {}
    assert(mode < 7);
    rounds = (unsigned)strtoul(argv[3], NULL, 10);
    assert(rounds && rounds <= 8192);
    if (!strcmp(argv[2], "pinned")) {
        cpu_set_t cpus; CPU_ZERO(&cpus);
#ifdef TRAMES_HOST_ORACLE
        assert(!sched_getaffinity(0, sizeof cpus, &cpus));
        int cpu;
        for (cpu = 0; cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &cpus); cpu++) {}
        assert(cpu < CPU_SETSIZE);
        CPU_ZERO(&cpus); CPU_SET(cpu, &cpus);
#else
        CPU_SET(0, &cpus);
#endif
        assert(!sched_setaffinity(0, sizeof cpus, &cpus));
    } else assert(!strcmp(argv[2], "spread"));
    assert(!socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ends));
    if (mode == 6) assert(!pipe(rights_pipe));
    if (mode == 4) {
        for (unsigned id = 0; id < 2; id++) {
            poller[id] = epoll_create1(EPOLL_CLOEXEC); assert(poller[id] >= 0);
            struct epoll_event event = {.events = EPOLLIN | EPOLLET, .data.u64 = id + 42};
            assert(!epoll_ctl(poller[id], EPOLL_CTL_ADD, ends[id], &event));
        }
    }
    send_token(1);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, exchange, (void *)(uintptr_t)1));
    exchange(NULL);
    assert(!pthread_join(thread, NULL));
    assert(payload[0] == value(rounds - 1, 1) && payload[4095] == value(rounds - 1, 1));
    sums[0] += value(rounds - 1, 1);
    receive(0); // Drain the last token/rights, then check half-close and EPIPE.
    assert(!shutdown(ends[1], SHUT_WR));
    char ch;
    assert(recv(ends[0], &ch, 1, MSG_WAITALL) == 0);
    assert(!shutdown(ends[1], SHUT_RD));
    assert(send(ends[0], "x", 1, MSG_NOSIGNAL) == -1 && errno == EPIPE);
    for (unsigned id = 0; id < 2; id++) {
        assert(!close(ends[id]));
        if (mode == 4) assert(!close(poller[id]));
        if (mode == 6) assert(!close(rights_pipe[id]));
    }
    printf("worker-unix %s %s %u %llu\n", argv[1], argv[2], rounds, (unsigned long long)(sums[0] + sums[1]));
    return 0;
}
