// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone Linux oracle for the ioctl used by Firefox's Wayland proxy.
// This is a diagnostic: current unsupported operations print their errno.
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static unsigned failures;

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
    if (stream(0) || stream(1)) { perror("probe setup/read/write"); return 2; }
    printf("fionread failures=%u\n", failures);
    return failures ? 1 : 0;
}
