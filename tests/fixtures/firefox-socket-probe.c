// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone Linux oracle for the ioctl used by Firefox's Wayland proxy.
// This is a diagnostic: current unsupported operations print their errno.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static unsigned failures;

static int message_flags(void) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) return 2;
    int fd = open("/dev/null", O_RDONLY);
    if (fd < 0) return 2;
    int carried[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, carried)) return 2;
    for (int round = 0; round < 4; round++) {
        int cloexec = round & 1;
        int sent = round == 3 ? pair[1] : round == 2 ? carried[0] : fd;
        char byte = 'x';
        struct iovec vector = {&byte, 1};
        union { struct cmsghdr alignment; char bytes[CMSG_SPACE(sizeof(int))]; } control;
        memset(&control, 0, sizeof control);
        struct msghdr message = {0};
        message.msg_iov = &vector;
        message.msg_iovlen = 1;
        message.msg_control = control.bytes;
        message.msg_controllen = sizeof control;
        struct cmsghdr *header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(header), &sent, sizeof sent);
        errno = 0;
        int result = (int)sendmsg(pair[1], &message, MSG_CMSG_CLOEXEC | MSG_DONTWAIT);
        printf("unix-sendmsg-cloexec round=%d result=%d errno=%d\n", round, result, errno);
        if (result != 1) { failures++; break; }
        memset(&control, 0, sizeof control);
        if (recvmsg(pair[0], &message, (cloexec ? MSG_CMSG_CLOEXEC : 0) | MSG_NOSIGNAL) != 1) return 2;
        header = CMSG_FIRSTHDR(&message);
        if (!header || header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS) return 2;
        int received;
        memcpy(&received, CMSG_DATA(header), sizeof received);
        if (byte != 'x' || fcntl(received, F_GETFD) != (cloexec ? FD_CLOEXEC : 0) || fcntl(sent, F_GETFD) != 0) return 2;
        if (round == 2 && (write(carried[1], "r", 1) != 1 || read(received, &byte, 1) != 1 || byte != 'r')) return 2;
        if (round == 3 && (write(received, "s", 1) != 1 || read(pair[0], &byte, 1) != 1 || byte != 's')) return 2;
        close(received);
    }
    close(fd); close(pair[0]); close(pair[1]); close(carried[0]); close(carried[1]);
    return 0;
}

static void available(const char *name, int fd, int expected, int legacy) {
    int bytes = -1;
    errno = 0;
    int result = ioctl(fd, FIONREAD, &bytes);
    int error = errno;
    // Some Linux kernels count the original Unix-stream skb length after a
    // partial read. Accept that one known variant; the next read checks the
    // actual remaining bytes and that this query did not consume any of them.
    printf("%s result=%d errno=%d bytes=%d expected=%d legacy=%d\n",
           name, result, error, bytes, expected, legacy);
    if (result != 0 || (bytes != expected && bytes != legacy)) failures++;
}

static int stream(int is_pipe) {
    int ends[2];
    if (is_pipe ? pipe(ends) : socketpair(AF_UNIX, SOCK_STREAM, 0, ends)) return 2;
    const char *kind = is_pipe ? "pipe-empty" : "unix-stream-empty";
    available(kind, ends[0], 0, 0);
    if (write(ends[1], "abcdefg", 7) != 7) return 2;
    available(is_pipe ? "pipe-queued" : "unix-stream-queued", ends[0], 7, 7);
    int alias = dup(ends[0]);
    if (alias < 0) return 2;
    char data[8];
    if (read(alias, data, 3) != 3 || memcmp(data, "abc", 3)) return 2;
    available(is_pipe ? "pipe-after-partial-read" : "unix-stream-after-partial-read", ends[0], 4, is_pipe ? 4 : 7);
    if (read(ends[0], data, 4) != 4 || memcmp(data, "defg", 4)) return 2;
    close(ends[1]);
    available(is_pipe ? "pipe-eof" : "unix-stream-eof", alias, 0, 0);
    close(alias);
    close(ends[0]);
    return 0;
}

int main(void) {
    if (stream(0) || stream(1) || message_flags()) { perror("probe setup/read/write"); return 2; }
    printf("fionread failures=%u\n", failures);
    return failures ? 1 : 0;
}
