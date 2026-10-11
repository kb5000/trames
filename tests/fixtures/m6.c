// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void events(void) {
    int fd = eventfd(2, EFD_SEMAPHORE | EFD_NONBLOCK | EFD_CLOEXEC);
    assert(fd >= 0 && (fcntl(fd, F_GETFD) & FD_CLOEXEC));
    uint64_t value = 0;
    assert(read(fd, &value, 8) == 8 && value == 1);
    int alias = dup(fd);
    assert(read(alias, &value, 8) == 8 && value == 1);
    errno = 0; assert(read(fd, &value, 8) == -1 && errno == EAGAIN);
    value = UINT64_MAX;
    errno = 0; assert(write(fd, &value, 8) == -1 && errno == EINVAL);
    assert(close(alias) == 0 && close(fd) == 0);
    fd = eventfd(0, EFD_NONBLOCK);
    value = UINT64_MAX - 1;
    assert(write(fd, &value, 8) == 8);
    value = 1;
    errno = 0; assert(write(fd, &value, 8) == -1 && errno == EAGAIN);
    struct pollfd p = { .fd = fd, .events = POLLIN | POLLOUT };
    assert(poll(&p, 1, 0) == 1 && p.revents == POLLIN);
    assert(read(fd, &value, 8) == 8 && value == UINT64_MAX - 1);
    close(fd);
    fd = eventfd(0, 0);
    pid_t child = fork(); assert(child >= 0);
    if (child == 0) { value = 17; assert(write(fd, &value, 8) == 8); _exit(0); }
    assert(read(fd, &value, 8) == 8 && value == 17);
    int status; assert(waitpid(child, &status, 0) == child && status == 0);
    close(fd);
    puts("eventfd semaphore/overflow/shared blocking: ok");
}

static void epoll_tests(void) {
    _Static_assert(sizeof(struct epoll_event) == 16, "AArch64 event ABI");
    int ep = epoll_create1(EPOLL_CLOEXEC), fd = eventfd(1, EFD_NONBLOCK);
    assert(ep >= 0 && fd >= 0);
    struct epoll_event event = { .events = EPOLLIN | EPOLLONESHOT, .data.u64 = UINT64_C(0x1020304050607080) };
    assert(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event) == 0);
    errno = 0; assert(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event) == -1 && errno == EEXIST);
    errno = 0; assert(syscall(SYS_epoll_pwait, ep, (void *)1, 1, 0, 0, 8) == -1 && errno == EFAULT);
    struct epoll_event out = {0};
    assert(epoll_wait(ep, &out, 1, 0) == 1 && out.events == EPOLLIN && out.data.u64 == event.data.u64);
    assert(epoll_wait(ep, &out, 1, 0) == 0);
    assert(epoll_ctl(ep, EPOLL_CTL_MOD, fd, &event) == 0);
    assert(epoll_wait(ep, &out, 1, 0) == 1);
    assert(epoll_ctl(ep, EPOLL_CTL_DEL, fd, NULL) == 0);
    event.events = EPOLLIN;
    assert(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event) == 0);
    int alias = dup(fd);
    event.data.u64 = 33;
    assert(epoll_ctl(ep, EPOLL_CTL_ADD, alias, &event) == 0);
    struct epoll_event pair[2];
    assert(epoll_wait(ep, pair, 2, 0) == 2);
    close(fd);
    assert(epoll_wait(ep, pair, 2, 0) == 2); // watch holds the open description identity
    close(alias);
    assert(epoll_wait(ep, pair, 2, 0) == 0);
    int a = eventfd(1, EFD_NONBLOCK), b = eventfd(1, EFD_NONBLOCK);
    event.data.u64 = 1; assert(epoll_ctl(ep, EPOLL_CTL_ADD, a, &event) == 0);
    event.data.u64 = 2; assert(epoll_ctl(ep, EPOLL_CTL_ADD, b, &event) == 0);
    assert(epoll_wait(ep, &out, 1, 0) == 1); uint64_t first = out.data.u64;
    assert(epoll_wait(ep, &out, 1, 0) == 1 && first != out.data.u64);
    close(a); close(b); close(ep);
    puts("epoll fault preflight/oneshot/dup lifetime/fairness: ok");
}

static volatile sig_atomic_t signalled;
static void handler(int signal) { (void)signal; signalled = 1; }
static void timers(void) {
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    int ep = epoll_create1(0);
    assert(fd >= 0 && ep >= 0);
    struct epoll_event event = { .events = EPOLLIN, .data.u64 = 42 }, out;
    assert(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event) == 0);
    struct itimerspec set = { .it_value.tv_nsec = 2000000 }, get;
    assert(timerfd_settime(fd, 0, &set, &get) == 0 && get.it_value.tv_sec == 0 && get.it_value.tv_nsec == 0);
    assert(epoll_wait(ep, &out, 1, 500) == 1 && out.data.u64 == 42);
    uint64_t count;
    errno = 0; assert(syscall(SYS_read, fd, (void *)1, 8) == -1 && errno == EFAULT);
    assert(read(fd, &count, 8) == 8 && count == 1);
    errno = 0; assert(read(fd, &count, 8) == -1 && errno == EAGAIN);
    assert(clock_gettime(CLOCK_MONOTONIC, &set.it_value) == 0);
    set.it_value.tv_nsec += 2000000;
    if (set.it_value.tv_nsec >= 1000000000) { ++set.it_value.tv_sec; set.it_value.tv_nsec -= 1000000000; }
    assert(timerfd_settime(fd, TFD_TIMER_ABSTIME, &set, NULL) == 0);
    struct timespec timeout = { .tv_nsec = 500000000 };
    int result = syscall(441, ep, &out, 1, &timeout, NULL, 8);
    if (getenv("TRAMES_REQUIRE_EPOLL_PWAIT2")) { assert(result == 1); }
    if (result == -1 && errno == ENOSYS) {
        // QEMU 10.0.13 has no guest epoll_pwait2 entry. The Trames harness
        // separately requires syscall 441 to succeed without this fallback.
        assert(epoll_wait(ep, &out, 1, 500) == 1);
    } else { assert(result == 1); }
    assert(read(fd, &count, 8) == 8 && count == 1);
    struct sigaction action = { .sa_handler = handler, .sa_flags = SA_ONSTACK }; sigemptyset(&action.sa_mask);
    assert(sigaction(SIGUSR1, &action, NULL) == 0);
    struct sigaction queried; assert(sigaction(SIGUSR1, NULL, &queried) == 0 && (queried.sa_flags & SA_ONSTACK));
    sigset_t blocked, original; sigemptyset(&blocked); sigaddset(&blocked, SIGUSR1);
    assert(sigprocmask(SIG_BLOCK, &blocked, &original) == 0);
    assert(raise(SIGUSR1) == 0);
    errno = 0; assert(epoll_pwait(ep, &out, 1, 500, &original) == -1 && errno == EINTR && signalled);
    sigset_t current; assert(sigprocmask(SIG_SETMASK, NULL, &current) == 0 && sigismember(&current, SIGUSR1));
    assert(sigprocmask(SIG_SETMASK, &original, NULL) == 0);
    close(ep); close(fd);
    puts("timerfd relative/absolute/fault, epoll_pwait2/signal mask: ok");
}

static void locks(void) {
    char path[] = "/tmp/trames-lock-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0);
    int pipefd[2]; assert(pipe(pipefd) == 0);
    struct flock lock = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 4, .l_len = 8 };
    assert(fcntl(fd, F_SETLK, &lock) == 0);
    pid_t parent = getpid(), child = fork(); assert(child >= 0);
    if (!child) {
        close(pipefd[0]);
        struct flock query = lock;
        assert(fcntl(fd, F_GETLK, &query) == 0 && query.l_type == F_WRLCK && query.l_pid == parent && query.l_start == 4 && query.l_len == 8);
        errno = 0; assert(fcntl(fd, F_SETLK, &lock) == -1 && errno == EAGAIN);
        assert(write(pipefd[1], "r", 1) == 1);
        assert(fcntl(fd, F_SETLKW, &lock) == 0);
        lock.l_type = F_UNLCK; assert(fcntl(fd, F_SETLK, &lock) == 0);
        close(fd); _exit(0);
    }
    close(pipefd[1]); char byte; assert(read(pipefd[0], &byte, 1) == 1);
    // Closing a dup releases this process's record locks, despite fd remaining.
    int alias = dup(fd); assert(alias >= 0); assert(close(alias) == 0);
    int status; assert(waitpid(child, &status, 0) == child && status == 0);
    lock.l_type = F_WRLCK; lock.l_start = 0; lock.l_len = 0;
    assert(fcntl(fd, F_SETLK, &lock) == 0);
    lock.l_type = F_UNLCK; lock.l_start = 4; lock.l_len = 8;
    assert(fcntl(fd, F_SETLK, &lock) == 0);
    child = fork(); assert(child >= 0);
    if (!child) {
        lock.l_type = F_WRLCK; assert(fcntl(fd, F_SETLK, &lock) == 0);
        lock.l_start = 0; lock.l_len = 4;
        errno = 0; assert(fcntl(fd, F_SETLK, &lock) == -1 && errno == EAGAIN);
        _exit(0);
    }
    assert(waitpid(child, &status, 0) == child && status == 0);
    close(pipefd[0]); assert(pipe(pipefd) == 0);
    lock.l_type = F_UNLCK; lock.l_start = 0; lock.l_len = 0;
    assert(fcntl(fd, F_SETLK, &lock) == 0);
    lock.l_type = F_WRLCK; lock.l_len = 1;
    assert(fcntl(fd, F_SETLK, &lock) == 0);
    child = fork(); assert(child >= 0);
    if (!child) {
        close(pipefd[0]); lock.l_start = 1;
        assert(fcntl(fd, F_SETLK, &lock) == 0);
        assert(write(pipefd[1], "r", 1) == 1);
        lock.l_start = 0;
        int result = fcntl(fd, F_SETLKW, &lock);
        int deadlock = result == -1 && errno == EDEADLK;
        assert(result == 0 || deadlock);
        close(fd); _exit(deadlock);
    }
    close(pipefd[1]); assert(read(pipefd[0], &byte, 1) == 1);
    lock.l_start = 1;
    int result = fcntl(fd, F_SETLKW, &lock);
    int deadlock = result == -1 && errno == EDEADLK;
    assert(result == 0 || deadlock);
    lock.l_type = F_UNLCK; lock.l_start = 0;
    assert(fcntl(fd, F_SETLK, &lock) == 0);
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status));
    assert(deadlock + WEXITSTATUS(status) == 1);
    close(fd); close(pipefd[0]); unlink(path);
    puts("POSIX locks conflict/query/wait/close/split/fork/deadlock: ok");
}

static void paths(void) {
    char path[] = "/tmp/trames-path-XXXXXX";
    assert(mkdtemp(path));
    char name[128]; snprintf(name, sizeof name, "%s/new/", path);
    assert(mkdir(name, 0700) == 0);
    struct stat st; assert(stat(name, &st) == 0 && S_ISDIR(st.st_mode));
    assert(rmdir(name) == 0);
    errno = 0; assert(stat(name, &st) == -1 && errno == ENOENT);
    errno = 0; assert(open(name, O_CREAT | O_RDWR, 0600) == -1 && errno == EISDIR);
    snprintf(name, sizeof name, "%s/link", path); assert(symlink("absent", name) == 0);
    char trailing[130]; snprintf(trailing, sizeof trailing, "%s/", name);
    errno = 0; assert(mkdir(trailing, 0700) == -1 && errno == EEXIST);
    unlink(name); rmdir(path);
    puts("trailing slash mkdir/stat/open/symlink semantics: ok");
}

static void fifos(void) {
    char dir[] = "/tmp/trames-fifo-XXXXXX";
    assert(mkdtemp(dir));
    char path[128]; snprintf(path, sizeof path, "%s/fifo", dir);
    assert(mkfifo(path, 0600) == 0);
    struct stat st; assert(stat(path, &st) == 0 && S_ISFIFO(st.st_mode));
    errno = 0; assert(open(path, O_WRONLY | O_NONBLOCK) == -1 && errno == ENXIO);
    int r = open(path, O_RDONLY | O_NONBLOCK); assert(r >= 0);
    struct pollfd pollfd = {r, POLLIN, 0}; assert(poll(&pollfd, 1, 0) == 0);
    int w = open(path, O_WRONLY | O_NONBLOCK); assert(w >= 0);
    assert(write(w, "fifo", 4) == 4); close(w);
    char bytes[8]; assert(read(r, bytes, 8) == 4 && !memcmp(bytes, "fifo", 4));
    assert(read(r, bytes, 8) == 0);
    assert(poll(&pollfd, 1, 0) == 1 && (pollfd.revents & POLLHUP)); close(r);
    struct sigaction action = { .sa_handler = handler }; sigemptyset(&action.sa_mask);
    assert(sigaction(SIGUSR1, &action, NULL) == 0);
    pid_t interrupted = fork(); assert(interrupted >= 0);
    if (!interrupted) { usleep(10000); kill(getppid(), SIGUSR1); _exit(0); }
    errno = 0; assert(open(path, O_RDONLY) == -1 && errno == EINTR);
    int interrupted_status; assert(waitpid(interrupted, &interrupted_status, 0) == interrupted && interrupted_status == 0);
    errno = 0; assert(open(path, O_WRONLY | O_NONBLOCK) == -1 && errno == ENXIO);
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        usleep(10000); int fd = open(path, O_WRONLY); assert(fd >= 0);
        assert(write(fd, "child", 5) == 5); close(fd); _exit(0);
    }
    r = open(path, O_RDONLY); assert(r >= 0);
    assert(read(r, bytes, 8) == 5 && !memcmp(bytes, "child", 5));
    assert(read(r, bytes, 8) == 0); close(r);
    int status; assert(waitpid(child, &status, 0) == child && status == 0);
    child = fork(); assert(child >= 0);
    if (!child) {
        usleep(10000); int fd = open(path, O_RDONLY); assert(fd >= 0);
        assert(read(fd, bytes, 8) == 3 && !memcmp(bytes, "out", 3)); close(fd); _exit(0);
    }
    w = open(path, O_WRONLY); assert(w >= 0);
    assert(write(w, "out", 3) == 3); close(w);
    assert(waitpid(child, &status, 0) == child && status == 0);
    int both = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC); assert(both >= 0);
    int nonblock = 0;
    assert(ioctl(both, FIONBIO, &nonblock) == 0 && !(fcntl(both, F_GETFL) & O_NONBLOCK));
    assert(fcntl(both, F_GETFD) & FD_CLOEXEC);
    int alias = dup(both); close(both); assert(unlink(path) == 0);
    assert(ioctl(alias, FIONBIO, &nonblock) == 0 && !(fcntl(alias, F_GETFL) & O_NONBLOCK));
    nonblock = 1;
    assert(ioctl(alias, FIONBIO, &nonblock) == 0 && (fcntl(alias, F_GETFL) & O_NONBLOCK));
    assert(write(alias, "alias", 5) == 5);
    assert(read(alias, bytes, 8) == 5 && !memcmp(bytes, "alias", 5));
    errno = 0; assert(lseek(alias, 0, SEEK_SET) == -1 && errno == ESPIPE);
    close(alias); rmdir(dir);
    puts("named FIFO nonblocking/open handshake/fork/EOF/dup/unlink: ok");
}

static void sockets(void) {
    for (int kind = SOCK_STREAM; kind <= SOCK_DGRAM; ++kind) {
        int fds[2]; assert(socketpair(AF_UNIX, kind | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds) == 0);
        assert(fcntl(fds[0], F_GETFD) & FD_CLOEXEC);
        assert(send(fds[0], "hello", 5, MSG_NOSIGNAL) == 5);
        char bytes[8] = {0};
        errno = 0; assert(syscall(SYS_recvfrom, fds[1], (void *)1, 5, 0, NULL, NULL) == -1 && errno == EFAULT);
        assert(recv(fds[1], bytes, 2, MSG_PEEK) == 2 && memcmp(bytes, "he", 2) == 0);
        if (kind == SOCK_DGRAM) { assert(recv(fds[1], bytes, 2, MSG_TRUNC) == 5); }
        else { assert(read(fds[1], bytes, 5) == 5 && memcmp(bytes, "hello", 5) == 0); }
        errno = 0; assert(recv(fds[1], bytes, sizeof bytes, 0) == -1 && errno == EAGAIN);
        int type = 0; socklen_t len = sizeof type;
        assert(getsockopt(fds[0], SOL_SOCKET, SO_TYPE, &type, &len) == 0 && type == kind && len == 4);
        struct sockaddr address; len = sizeof address;
        assert(getsockname(fds[0], &address, &len) == 0 && len == 2 && address.sa_family == AF_UNIX);
        close(fds[0]); close(fds[1]);
    }
    int fds[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    pid_t child = fork(); assert(child >= 0);
    if (!child) { close(fds[0]); assert(write(fds[1], "child", 5) == 5); close(fds[1]); _exit(0); }
    close(fds[1]); char bytes[8]; assert(read(fds[0], bytes, 8) == 5 && memcmp(bytes, "child", 5) == 0);
    assert(read(fds[0], bytes, 8) == 0);
    int status; assert(waitpid(child, &status, 0) == child && status == 0); close(fds[0]);
    puts("AF_UNIX pairs stream/datagram/peek/trunc/fault/fork EOF: ok");
}

int main(void) {
    if (mkdir("/tmp", 01777) == -1) { assert(errno == EEXIST); }
    events(); epoll_tests(); timers(); sockets(); locks(); paths(); fifos(); return 0;
}
