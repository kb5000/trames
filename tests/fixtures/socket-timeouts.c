/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s errno=%d\n", __LINE__, #x, errno); exit(1); } } while (0)
static volatile sig_atomic_t caught;
static void handler(int sig) { caught = sig; }
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "seccomp") == 0) {
        struct sock_filter instructions[] = {
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 0),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_getppid, 0, 1),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EACCES),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        };
        struct sock_fprog program = {.len = 4, .filter = instructions};
        CHECK(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
        CHECK(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program, 0, 0) == 0);
        CHECK(prctl(PR_GET_SECCOMP, 0, 0, 0, 0) == SECCOMP_MODE_FILTER);
        CHECK(syscall(SYS_getppid) == -1 && errno == EACCES);
        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0) {
            CHECK(syscall(SYS_getppid) == -1 && errno == EACCES);
            _exit(0);
        }
        int status;
        CHECK(waitpid(child, &status, 0) == child && status == 0);
        puts("prctl seccomp: filter install, enforcement and fork inheritance passed");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "devices") == 0) {
        int allowed = open("/dev/null", O_RDWR);
        CHECK(allowed >= 0);
        CHECK(close(allowed) == 0);
        CHECK(mknod("/tmp/denied-device", S_IFCHR | 0600, makedev(1, 1)) == 0);
        CHECK(open("/tmp/denied-device", O_RDONLY) == -1 && errno == EPERM);
        CHECK(unlink("/tmp/denied-device") == 0);
        puts("DEFAULT_DEVICE_POLICY_OK");
        return 0;
    }
    int netlink = argc == 2 && strcmp(argv[1], "netlink") == 0;
    int fd = socket(netlink ? AF_NETLINK : AF_INET, netlink ? SOCK_RAW : SOCK_DGRAM, 0);
    CHECK(fd >= 0);
    char path[80], target[128];
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    int link = open(path, O_PATH | O_NOFOLLOW);
    CHECK(link >= 0);
    ssize_t count = readlinkat(link, "", target, sizeof(target));
    CHECK(count >= 8 && memcmp(target, "socket:[", 8) == 0);
    CHECK(close(link) == 0);
    CHECK(readlinkat(fd, "", target, sizeof(target)) == -1 && errno == ENOENT);
    struct sockaddr_nl address = {.nl_family = AF_NETLINK};
    struct sockaddr_in udp = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    CHECK(bind(fd, netlink ? (struct sockaddr *)&address : (struct sockaddr *)&udp,
               netlink ? sizeof(address) : sizeof(udp)) == 0);
    struct timeval timeout = {.tv_usec = 20000}, observed;
    CHECK(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    socklen_t size = sizeof(observed);
    CHECK(getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &observed, &size) == 0);
    CHECK(size == sizeof(observed) && observed.tv_sec == 0 && observed.tv_usec >= 20000);
    char byte;
    CHECK(recv(fd, &byte, 1, 0) == -1 && errno == EAGAIN);
    CHECK(recv(fd, &byte, 1, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    CHECK(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0);
    timeout.tv_usec = 1000000;
    CHECK(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1 && errno == EDOM);
    CHECK(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, 15) == -1 && errno == EINVAL);
    CHECK(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (void *)1, sizeof(timeout)) == -1 && errno == EFAULT);
    size = sizeof(observed);
    CHECK(getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &observed, &size) == 0 && observed.tv_usec >= 20000);
    struct pollfd pollfd = {.fd = fd, .events = POLLIN};
    CHECK(poll(&pollfd, 1, 30) == 0);
    int duplicate = dup(fd);
    CHECK(duplicate >= 0);
    timeout = (struct timeval){.tv_sec = 1};
    CHECK(setsockopt(duplicate, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    struct sigaction action = {.sa_handler = handler, .sa_flags = SA_RESTART};
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, &action, NULL) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        usleep(20000);
        CHECK(kill(getppid(), SIGUSR1) == 0);
        _exit(0);
    }
    CHECK(recv(fd, &byte, 1, 0) == -1 && errno == EINTR && caught == SIGUSR1);
    int status;
    CHECK(waitpid(child, &status, 0) == child && status == 0);
    timeout = (struct timeval){0};
    CHECK(setsockopt(duplicate, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    size = sizeof(observed);
    CHECK(getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &observed, &size) == 0);
    CHECK(observed.tv_sec == 0 && observed.tv_usec == 0);
    CHECK(recv(fd, &byte, 1, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    CHECK(close(duplicate) == 0 && close(fd) == 0);
    puts("socket timeouts: blocking expiry, nonblocking, timeval/errors, dup/fork, SA_RESTART interruption and reset passed");
    return 0;
}
