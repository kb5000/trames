// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s: %s\n", __LINE__, #x, strerror(errno)); exit(1); } } while (0)

static socklen_t address(struct sockaddr_un *out, const char *label) {
    memset(out, 0, sizeof(*out));
    out->sun_family = AF_UNIX;
    int n = snprintf(out->sun_path + 1, sizeof(out->sun_path) - 1,
                     "trames-notify-%ld-%s", (long)getpid(), label);
    CHECK(n > 0 && n < (int)sizeof(out->sun_path) - 1);
    return offsetof(struct sockaddr_un, sun_path) + 1 + n;
}

int main(void) {
    alarm(20);
    int server = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    CHECK(server >= 0);
    int enabled = 1;
    CHECK(setsockopt(server, SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    socklen_t size = sizeof(enabled);
    enabled = 0;
    CHECK(getsockopt(server, SOL_SOCKET, SO_PASSCRED, &enabled, &size) == 0 && enabled == 1);
    struct sockaddr_un destination;
    socklen_t destination_size = address(&destination, "server");
    CHECK(bind(server, (void *)&destination, destination_size) == 0);
    int ready[2], ack[2];
    CHECK(pipe(ready) == 0 && pipe(ack) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        close(server); close(ready[0]); close(ack[1]);
        int client = socket(AF_UNIX, SOCK_DGRAM, 0);
        CHECK(client >= 0);
        struct sockaddr_un source;
        socklen_t source_size = address(&source, "client");
        CHECK(bind(client, (void *)&source, source_size) == 0);
        CHECK(connect(client, (void *)&destination, destination_size) == 0);
        struct iovec vector = { .iov_base = "READY=1", .iov_len = 7 };
        char control[CMSG_SPACE(sizeof(int))] = {0};
        struct msghdr msg = { .msg_iov = &vector, .msg_iovlen = 1,
            .msg_control = control, .msg_controllen = sizeof(control) };
        struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
        c->cmsg_len = CMSG_LEN(sizeof(int)); c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(c), &ready[1], sizeof(int));
        CHECK(sendmsg(client, &msg, 0) == 7);
        CHECK(write(ready[1], "r", 1) == 1);
        char byte;
        CHECK(read(ack[0], &byte, 1) == 1);
        _exit(0);
    }
    close(ready[1]); close(ack[0]);
    char byte;
    CHECK(read(ready[0], &byte, 1) == 1);
    for (int peek = 1; peek >= 0; peek--) {
        char data[32] = {0}, control[128] = {0};
        struct sockaddr_un source;
        struct iovec vector = { .iov_base = data, .iov_len = sizeof(data) };
        struct msghdr msg = { .msg_name = &source, .msg_namelen = sizeof(source),
            .msg_iov = &vector, .msg_iovlen = 1,
            .msg_control = control, .msg_controllen = sizeof(control) };
        CHECK(recvmsg(server, &msg, peek ? MSG_PEEK : MSG_CMSG_CLOEXEC) == 7);
        CHECK(memcmp(data, "READY=1", 7) == 0 && (msg.msg_flags & ~MSG_CMSG_CLOEXEC) == 0);
        CHECK(msg.msg_namelen > offsetof(struct sockaddr_un, sun_path) + 1 && source.sun_path[0] == 0);
        int credentials = 0, rights = 0;
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
            CHECK(c->cmsg_level == SOL_SOCKET);
            if (c->cmsg_type == SCM_CREDENTIALS) {
                struct ucred value; memcpy(&value, CMSG_DATA(c), sizeof(value));
                CHECK(value.pid == child && value.uid == getuid() && value.gid == getgid());
                credentials++;
            } else if (c->cmsg_type == SCM_RIGHTS) {
                int fd; memcpy(&fd, CMSG_DATA(c), sizeof(fd));
                CHECK(write(fd, "f", 1) == 1);
                CHECK(read(ready[0], &byte, 1) == 1 && byte == 'f');
                CHECK(!!(fcntl(fd, F_GETFD) & FD_CLOEXEC) == !peek);
                close(fd); rights++;
            } else CHECK(0);
        }
        CHECK(credentials == 1 && rights == 1);
    }
    CHECK(write(ack[1], "a", 1) == 1);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    int client = socket(AF_UNIX, SOCK_DGRAM, 0);
    CHECK(client >= 0);
    CHECK(sendto(client, "payload", 7, 0, (void *)&destination, destination_size) == 7);
    char data[2], control[CMSG_SPACE(sizeof(struct ucred))];
    struct iovec vector = { .iov_base = data, .iov_len = sizeof(data) };
    struct msghdr msg = { .msg_iov = &vector, .msg_iovlen = 1,
        .msg_control = control, .msg_controllen = sizeof(control) };
    CHECK(recvmsg(server, &msg, MSG_TRUNC) == 7 && (msg.msg_flags & MSG_TRUNC));
    CHECK(memcmp(data, "pa", 2) == 0);
    close(client); close(server); close(ready[0]); close(ack[1]);
    int pair[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    enabled = 1;
    CHECK(setsockopt(pair[0], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        close(pair[0]);
        CHECK(write(pair[1], "stream", 6) == 6);
        char response;
        CHECK(read(pair[1], &response, 1) == 1);
        _exit(0);
    }
    close(pair[1]);
    char stream[8];
    vector.iov_base = stream; vector.iov_len = sizeof(stream);
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &vector; msg.msg_iovlen = 1;
    msg.msg_control = control; msg.msg_controllen = sizeof(control);
    CHECK(recvmsg(pair[0], &msg, 0) == 6 && memcmp(stream, "stream", 6) == 0);
    struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
    CHECK(c && c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_CREDENTIALS);
    struct ucred identity; memcpy(&identity, CMSG_DATA(c), sizeof(identity));
    CHECK(identity.pid == child && identity.uid == getuid() && identity.gid == getgid());
    CHECK(write(pair[0], "a", 1) == 1);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(pair[0]);
    puts("unix-notify: addressed datagrams, sender credentials, peek, rights, close-on-exec and truncation: ok");
    puts("unix-stream: forked writer credentials on the direct byte path: ok");
    return 0;
}
