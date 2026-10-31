// SPDX-License-Identifier: GPL-3.0-or-later
// Native Linux runs this as root in a newly private mount namespace.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/personality.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s: %s\n",__LINE__,#x,strerror(errno)); exit(1); } } while (0)

static void add(int ep, int fd, unsigned events, unsigned long long id) {
    struct epoll_event e = {.events = events, .data.u64 = id};
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &e) == 0);
}
static void wait_event(int ep, unsigned long long id) {
    struct epoll_event e;
    CHECK(epoll_wait(ep, &e, 1, 1000) == 1 && (e.events & EPOLLIN) && e.data.u64 == id);
}
static void empty(int ep) {
    struct epoll_event e;
    CHECK(epoll_wait(ep, &e, 1, 0) == 0);
}
static int contains_mount(int fd, const char *path) {
    char text[65536];
    ssize_t n = pread(fd, text, sizeof(text) - 1, 0);
    CHECK(n >= 0); text[n] = 0;
    char field[512];
    CHECK(snprintf(field, sizeof(field), " %s ", path) > 0);
    return strstr(text, field) != NULL;
}
int main(void) {
    alarm(25);
    CHECK(unshare(CLONE_NEWNS) == 0);
    CHECK(mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == 0);
    CHECK(mkdir("/tmp", 01777) == 0 || errno == EEXIST);
    char directory[] = "/tmp/trames-mounts-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char a[256], b[256], file[256], isolated[256];
    snprintf(a, sizeof(a), "%s/a", directory);
    snprintf(b, sizeof(b), "%s/b", directory);
    snprintf(file, sizeof(file), "%s/file", directory);
    snprintf(isolated, sizeof(isolated), "%s/isolated", directory);
    CHECK(mkdir(a, 0755) == 0 && mkdir(b, 0755) == 0 && mkdir(isolated, 0755) == 0);
    int fd = open(file, O_CREAT | O_RDWR | O_NOATIME, 0644);
    CHECK(fd >= 0 && write(fd, "x", 1) == 1);
    struct timespec times[2] = {{.tv_sec = 1}, {.tv_sec = 1}};
    CHECK(futimens(fd, times) == 0);
    char byte; struct stat st;
    CHECK(fstatat(AT_FDCWD, file, &st, AT_NO_AUTOMOUNT) == 0);
    CHECK(pread(fd, &byte, 1, 0) == 1 && fstat(fd, &st) == 0 && st.st_atim.tv_sec == 1);
    CHECK(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NOATIME) == 0);
    CHECK(pread(fd, &byte, 1, 0) == 1 && fstat(fd, &st) == 0 && st.st_atim.tv_sec > 1);
    CHECK(close(fd) == 0 && unlink(file) == 0);
    CHECK(personality(0xffffffffUL) == 0 && personality(0) == 0);
    puts("open-noatime: timestamps and mutable flags; native Linux personality query: ok");

    int info = open("/proc/self/mountinfo", O_RDONLY | O_CLOEXEC);
    CHECK(info >= 0);
    struct pollfd pollfd = {.fd = info, .events = POLLPRI};
    CHECK(poll(&pollfd, 1, 0) == 0);
    int monitor = epoll_create1(EPOLL_CLOEXEC), outer = epoll_create1(EPOLL_CLOEXEC);
    CHECK(monitor >= 0 && outer >= 0);
    add(monitor, info, EPOLLIN | EPOLLET, 0x123456789abcdef0ULL);
    add(outer, monitor, EPOLLIN, 0xfedcba9876543210ULL);
    struct epoll_event cycle = {.events = EPOLLIN};
    CHECK(epoll_ctl(monitor, EPOLL_CTL_ADD, outer, &cycle) < 0 && errno == ELOOP);
    wait_event(outer, 0xfedcba9876543210ULL);
    wait_event(monitor, 0x123456789abcdef0ULL);
    empty(monitor); empty(outer);
    int notify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    CHECK(notify >= 0);
    CHECK(inotify_add_watch(notify, directory, IN_CREATE | IN_ISDIR) >= 0);
    CHECK(mount("tmpfs", a, "tmpfs", MS_NOSUID | MS_NODEV, "size=4m,mode=755") == 0);
    wait_event(outer, 0xfedcba9876543210ULL);
    wait_event(monitor, 0x123456789abcdef0ULL);
    empty(monitor); empty(outer);
    CHECK(contains_mount(info, a));
    char payload[300]; snprintf(payload, sizeof(payload), "%s/payload", a);
    fd = open(payload, O_CREAT | O_RDWR, 0600);
    CHECK(fd >= 0 && write(fd, "kept", 4) == 4 && fstat(fd, &st) == 0);
    CHECK(mount(a, b, NULL, MS_MOVE, NULL) == 0);
    wait_event(outer, 0xfedcba9876543210ULL);
    wait_event(monitor, 0x123456789abcdef0ULL);
    empty(monitor); empty(outer);
    CHECK(!contains_mount(info, a) && contains_mount(info, b));
    char moved[300]; snprintf(moved, sizeof(moved), "%s/payload", b);
    struct stat after;
    CHECK(stat(moved, &after) == 0 && after.st_dev == st.st_dev && after.st_ino == st.st_ino);
    char data[4]; CHECK(pread(fd, data, 4, 0) == 4 && memcmp(data, "kept", 4) == 0);
    CHECK(mount(b, b, NULL, MS_MOVE, NULL) < 0 && errno == ELOOP);
    puts("mount-events: nested epoll, edge delivery, moved identity and live mountinfo: ok");

    pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        CHECK(unshare(CLONE_NEWNS) == 0);
        CHECK(mount("tmpfs", isolated, "tmpfs", 0, "size=4m") == 0);
        CHECK(!contains_mount(info, isolated));
        int own = open("/proc/self/mountinfo", O_RDONLY); CHECK(own >= 0);
        CHECK(contains_mount(own, isolated));
        _exit(0);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    empty(monitor); empty(outer);
    CHECK(close(fd) == 0 && umount(b) == 0);
    wait_event(outer, 0xfedcba9876543210ULL);
    wait_event(monitor, 0x123456789abcdef0ULL);
    CHECK(!contains_mount(info, b));
    CHECK(close(info) == 0 && close(monitor) == 0 && close(outer) == 0 && close(notify) == 0);
    CHECK(rmdir(a) == 0 && rmdir(b) == 0 && rmdir(isolated) == 0 && rmdir(directory) == 0);
    puts("mount-namespace: open namespace pinning, private changes and unmount notification: ok");
    return 0;
}
