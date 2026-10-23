// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

enum { ROUNDS = 128 };
static pthread_barrier_t barrier;
static void gate(void) {
    int value = pthread_barrier_wait(&barrier);
    assert(!value || value == PTHREAD_BARRIER_SERIAL_THREAD);
}
static void check(void) {
    struct utsname u; assert(!uname(&u));
    assert(!strcmp(u.sysname, "Linux") && !strcmp(u.machine, "aarch64"));
    assert(u.nodename[0] == 'a' || u.nodename[0] == 'b');
    assert(u.domainname[0] == 'd' || u.domainname[0] == 'e');
    for (int i = 1; i < 64; ++i) {
        assert(u.nodename[i] == u.nodename[0]);
        assert(u.domainname[i] == u.domainname[0]);
    }
    assert(!u.nodename[64] && !u.domainname[64]);
}
static void *worker(void *which) {
    char hostname[64], domain[64];
    memset(hostname, which ? 'a' : 'b', sizeof(hostname));
    memset(domain, which ? 'd' : 'e', sizeof(domain));
    gate();
    for (int i = 0; i < ROUNDS; ++i) {
        assert(!sethostname(hostname, sizeof(hostname))); check();
        assert(!setdomainname(domain, sizeof(domain))); check();
    }
    gate();
    return NULL;
}
int main(void) {
    char hostname[64], domain[64]; memset(hostname, 'a', 64); memset(domain, 'd', 64);
    assert(!sethostname(hostname, 64) && !setdomainname(domain, 64));
    assert(sethostname(hostname, 65) == -1 && errno == EINVAL);
    assert(sethostname((void *)1, 1) == -1 && errno == EFAULT);
    assert(uname((void *)1) == -1 && errno == EFAULT);
    check();
    assert(!pthread_barrier_init(&barrier, NULL, 2));
    pthread_t thread; assert(!pthread_create(&thread, NULL, worker, (void *)1));
    worker(NULL); assert(!pthread_join(thread, NULL));
    assert(!pthread_barrier_destroy(&barrier));
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        assert(!setuid(1000)); check();
        assert(sethostname(hostname, 64) == -1 && errno == EPERM);
        assert(setdomainname(domain, 64) == -1 && errno == EPERM);
        _exit(0);
    }
    int status; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    puts("worker-uts operations=1024: concurrent names, faults and authority: ok");
    return 0;
}
