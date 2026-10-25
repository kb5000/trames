// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <unistd.h>
#define MiB (1024UL * 1024)
// Two forked copies must commit over 10 MiB, independent of untouched loader
// and stack zero pages. The aggregate request below must still fit one process.
#define Payload (5 * MiB)
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s errno=%d\n", __LINE__, #x, errno); exit(1); } } while (0)
static void reports(void) {
    struct sysinfo s;
    CHECK(sysinfo(&s) == 0 && s.totalram * s.mem_unit == 12 * MiB);
    CHECK(s.freeram < s.totalram);
    struct rlimit r;
    const int resources[] = {RLIMIT_AS, RLIMIT_DATA, RLIMIT_RSS};
    for (unsigned i = 0; i < sizeof resources / sizeof resources[0]; ++i) {
        CHECK(getrlimit(resources[i], &r) == 0 && r.rlim_cur == 8 * MiB && r.rlim_max == 8 * MiB);
    }
    FILE *f = fopen("/proc/meminfo", "r");
    unsigned long total;
    CHECK(f && fscanf(f, "MemTotal: %lu kB", &total) == 1 && total == 12 * 1024);
    CHECK(fclose(f) == 0);
}
static void *thread(void *unused) {
    (void)unused;
    reports();
    void *p = mmap(NULL, MiB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(p != MAP_FAILED);
    memset(p, 42, MiB);
    CHECK(munmap(p, MiB) == 0);
    return NULL;
}
int main(int argc, char **argv) {
    reports();
    if (argc == 2 && strcmp(argv[1], "exec") == 0) return 0;
    void *p = mmap(NULL, Payload, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(p != MAP_FAILED);
    memset(p, 7, Payload);
    CHECK(mmap(NULL, 4 * MiB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED && errno == ENOMEM);
    pthread_attr_t attr;
    pthread_t tid;
    CHECK(pthread_attr_init(&attr) == 0 && pthread_attr_setstacksize(&attr, 65536) == 0);
    CHECK(pthread_create(&tid, &attr, thread, NULL) == 0 && pthread_join(tid, NULL) == 0);
    CHECK(pthread_attr_destroy(&attr) == 0);
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        // Each address space fits individually; the aggregate quota rejects
        // this allocation. Determine the request from the actual free budget.
        struct sysinfo s;
        CHECK(sysinfo(&s) == 0);
        size_t bytes = (s.freeram * s.mem_unit + 4095) / 4096 * 4096 + 4096;
        CHECK(bytes < 2 * MiB);
        // File mappings reserve committed quota during mmap. Anonymous zero
        // reservations defer that charge until first write, tested separately.
        int fd = memfd_create("aggregate-budget", 0);
        CHECK(fd >= 0 && ftruncate(fd, bytes) == 0);
        CHECK(mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) == MAP_FAILED && errno == ENOMEM);
        CHECK(close(fd) == 0);
        char *args[] = {"/memory-budget", "exec", NULL};
        execv(args[0], args);
        CHECK(0);
    }
    int status;
    CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        struct sysinfo s;
        CHECK(sysinfo(&s) == 0);
        size_t bytes = (s.freeram * s.mem_unit + 4095) / 4096 * 4096 + 4096;
        CHECK(bytes < 2 * MiB);
        volatile unsigned char *zero = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK((void *)zero != MAP_FAILED);
        // The faulting child is killed on first-touch exhaustion. Its parent
        // remains runnable and retains its original private bytes and budget.
        for (size_t i = 0; i < bytes; i += 4096) zero[i] = 42;
        CHECK(0);
    }
    CHECK(waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    CHECK(((unsigned char *)p)[0] == 7 && munmap(p, Payload) == 0);
    puts("memory budget: reports, process quota, shared threads, fork aggregate and exec passed");
    return 0;
}
