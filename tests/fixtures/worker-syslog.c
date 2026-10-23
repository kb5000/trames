// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

enum { ROUNDS = 128 };
static pthread_barrier_t barrier;
static const char marker[] = "shared-syslog-worker-marker";
static void *reader(void *unused) {
    (void)unused;
    int entered = pthread_barrier_wait(&barrier);
    assert(!entered || entered == PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned i = 0; i < ROUNDS; ++i) {
        char bytes[4096];
        int n = klogctl(3, bytes, sizeof(bytes) - 1);
        assert(n > 0 && n < (int)sizeof(bytes));
        bytes[n] = 0;
        assert(strstr(bytes, marker));
        assert(klogctl(9, NULL, 0) > 0);
        assert(klogctl(10, NULL, 0) >= 4096);
    }
    return NULL;
}
static void waited(pid_t child) {
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && !WEXITSTATUS(status));
}
int main(void) {
    int fd = open("/dev/kmsg", O_RDWR | O_NONBLOCK);
    assert(fd >= 0 && write(fd, marker, sizeof(marker) - 1) == (ssize_t)sizeof(marker) - 1);
    assert(klogctl(4, (void *)1, 100) == -1 && errno == EFAULT);
    assert(klogctl(2, (void *)1, 100) == -1 && errno == EFAULT);
    assert(klogctl(3, (void *)1, -1) == -1 && errno == EINVAL);
    assert(klogctl(11, NULL, 0) == -1 && errno == EINVAL);
    assert(!pthread_barrier_init(&barrier, NULL, 2));
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, reader, NULL));
    reader(NULL);
    assert(!pthread_join(thread, NULL));
    assert(!pthread_barrier_destroy(&barrier));
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        assert(!setuid(1000));
        assert(klogctl(10, NULL, 0) == -1 && errno == EPERM);
        assert(klogctl(5, NULL, 0) == -1 && errno == EPERM);
        _exit(0);
    }
    waited(child);
    child = fork(); assert(child >= 0);
    if (!child) {
        struct filter { uint16_t code; uint8_t yes, no; uint32_t value; } filters[] = {
            {0x20, 0, 0, 0}, {0x15, 0, 1, SYS_syslog},
            {6, 0, 0, 0x50000 | EACCES}, {6, 0, 0, 0x7fff0000}
        };
        struct { uint16_t count; struct filter *filters; } program = {4, filters};
        assert(!prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
        assert(!prctl(PR_SET_SECCOMP, 2, &program));
        assert(klogctl(5, NULL, 0) == -1 && errno == EACCES);
        _exit(0);
    }
    waited(child);
    char bytes[4096];
    int n = klogctl(4, bytes, sizeof(bytes) - 1);
    assert(n > 0); bytes[n] = 0; assert(strstr(bytes, marker));
    assert(klogctl(3, bytes, sizeof(bytes)) == 0);
    assert(klogctl(9, NULL, 0) > 0); // clear does not consume the legacy stream.
    while (klogctl(9, NULL, 0)) assert(klogctl(2, bytes, sizeof(bytes)) > 0);
    // An empty legacy stream blocks; do not treat it as a nonblocking query.
    n = read(fd, bytes, sizeof(bytes) - 1);
    assert(n > 0); bytes[n] = 0; assert(strstr(bytes, marker));
    assert(!close(fd));
    printf("worker-syslog queries=%u: shared records, clear, faults, authority and seccomp: ok\n", ROUNDS * 6);
    return 0;
}
