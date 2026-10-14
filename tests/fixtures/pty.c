// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static int slave_for(int master) {
    char name[128];
    assert(ptsname_r(master, name, sizeof name) == 0);
    assert(grantpt(master) == 0);
    assert(unlockpt(master) == 0);
    int slave = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    assert(slave >= 0);
    return slave;
}
static void raw(int fd) {
    struct termios t;
    assert(tcgetattr(fd, &t) == 0);
    cfmakeraw(&t);
    assert(tcsetattr(fd, TCSANOW, &t) == 0);
}
static volatile sig_atomic_t hungup;
static void on_hangup(int s) { (void)s; hungup = 1; }

int main(void) {
    int m = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    assert(m >= 0);
    int number, lock;
    assert(ioctl(m, TIOCGPTN, &number) == 0);
    // QEMU 10's user-mode ioctl translator omits TIOCGPTLCK. Native Linux
    // and Trames always require it; only the QEMU oracle skips this query.
    if (!getenv("TRAMES_QEMU_NO_PTLOCK_QUERY"))
        assert(ioctl(m, TIOCGPTLCK, &lock) == 0 && lock == 1);
    char path[128];
    assert(ptsname_r(m, path, sizeof path) == 0);
    errno = 0;
    assert(open(path, O_RDWR | O_NOCTTY) == -1 && errno == EIO);
    int s = slave_for(m);
    if (!getenv("TRAMES_QEMU_NO_PTLOCK_QUERY"))
        assert(ioctl(m, TIOCGPTLCK, &lock) == 0 && lock == 0);
    struct stat st;
    assert(fstat(s, &st) == 0 && S_ISCHR(st.st_mode));
    assert(lseek(s, 0, SEEK_CUR) == -1 && errno == ESPIPE);
    char b[4096];
    assert(write(m, "line\r", 5) == 5);
    assert(read(s, b, sizeof b) == 5 && memcmp(b, "line\n", 5) == 0);
    assert(read(m, b, sizeof b) == 6 && memcmp(b, "line\r\n", 6) == 0);
    raw(s);
    struct winsize w = { .ws_row = 43, .ws_col = 117 }, out;
    assert(ioctl(m, TIOCSWINSZ, &w) == 0);
    assert(ioctl(s, TIOCGWINSZ, &out) == 0 && out.ws_row == 43 && out.ws_col == 117);
    assert(write(s, "raw\n", 4) == 4);
    assert(read(m, b, sizeof b) == 4 && memcmp(b, "raw\n", 4) == 0);
    int m2 = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    assert(m2 >= 0);
    int s2 = slave_for(m2), number2;
    assert(ioctl(m2, TIOCGPTN, &number2) == 0 && number != number2);
    raw(s2);
    assert(write(s2, "separate", 8) == 8);
    assert(read(m2, b, sizeof b) == 8 && memcmp(b, "separate", 8) == 0);
    assert(read(m, b, sizeof b) == -1 && errno == EAGAIN);
    int packet = 1;
    assert(ioctl(m, TIOCPKT, &packet) == 0);
    assert(write(s, "packet", 6) == 6);
    assert(read(m, b, sizeof b) == 7 && b[0] == 0 && memcmp(b + 1, "packet", 6) == 0);
    packet = 0;
    assert(ioctl(m, TIOCPKT, &packet) == 0);
    int alias = dup(m);
    assert(alias >= 0 && close(m) == 0);
    assert(write(s, "last", 4) == 4 && read(alias, b, sizeof b) == 4);
    assert(close(alias) == 0);
    struct pollfd p = { .fd = s, .events = POLLIN };
    assert(poll(&p, 1, 0) == 1 && (p.revents & POLLHUP));
    assert(read(s, b, sizeof b) == 0);
    assert(write(s, "x", 1) == -1 && errno == EIO);
    close(s);
    assert(write(s2, "final", 5) == 5 && close(s2) == 0);
    assert(read(m2, b, sizeof b) == 5 && memcmp(b, "final", 5) == 0);
    assert(read(m2, b, sizeof b) == -1 && errno == EIO);
    s2 = slave_for(m2); // a live master permits reopening the same slave
    assert(write(s2, "again", 5) == 5 && read(m2, b, sizeof b) == 5);
    close(s2); close(m2);
    puts("pty allocation/lock/canonical/raw/resize/isolation/packet/alias/hangup/reopen: ok");

    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    pid_t child = fork(); assert(child >= 0);
    if (child == 0) {
        close(pair[0]);
        assert(write(pair[1], "abc", 3) == 3);
        usleep(20000);
        assert(write(pair[1], "defgh", 5) == 5);
        close(pair[1]); _exit(17);
    }
    close(pair[1]);
    assert(recv(pair[0], b, 8, MSG_WAITALL) == 8 && memcmp(b, "abcdefgh", 8) == 0);
    assert(recv(pair[0], b, 8, MSG_WAITALL) == 0);
    int status;
    assert(waitpid(child, &status, __WALL) == child && WIFEXITED(status) && WEXITSTATUS(status) == 17);
    close(pair[0]);
    puts("Unix MSG_WAITALL split record and __WALL child wait: ok");

    m = posix_openpt(O_RDWR | O_NOCTTY); assert(m >= 0);
    assert(ptsname_r(m, path, sizeof path) == 0 && unlockpt(m) == 0);
    int sync[2]; assert(pipe(sync) == 0);
    child = fork(); assert(child >= 0);
    if (child == 0) {
        close(sync[0]); close(m);
        assert(setsid() == getpid());
        s = open(path, O_RDWR | O_NOCTTY); assert(s >= 0);
        assert(ioctl(s, TIOCSCTTY, 0) == 0);
        assert(tcgetsid(s) == getpid() && tcgetpgrp(s) == getpid());
        int tty = open("/dev/tty", O_RDWR); assert(tty >= 0);
        sigset_t blocked, previous;
        sigemptyset(&blocked); sigaddset(&blocked, SIGHUP);
        assert(sigprocmask(SIG_BLOCK, &blocked, &previous) == 0);
        signal(SIGHUP, on_hangup);
        assert(write(sync[1], "r", 1) == 1);
        // HUP may arrive after the ready byte but before waiting. Atomically
        // unblock and suspend; a check followed by pause() loses that wakeup.
        while (!hungup) sigsuspend(&previous);
        assert(sigprocmask(SIG_SETMASK, &previous, NULL) == 0);
        close(tty); close(s); close(sync[1]); _exit(0);
    }
    close(sync[1]);
    assert(read(sync[0], b, 1) == 1);
    close(m);
    assert(waitpid(child, &status, __WALL) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(sync[0]);
    puts("PTY session/controlling tty/last-master SIGHUP: ok");
    return 0;
}
