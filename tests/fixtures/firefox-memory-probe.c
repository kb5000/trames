// SPDX-License-Identifier: GPL-3.0-or-later
// Original-musl/Linux oracle for a sparse BSS like Alpine's libscudo.so.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile unsigned char sparse[512u << 20] __attribute__((aligned(4096)));

static void require(int condition, const char *what) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
}

static int resident(void *page) {
    unsigned char result = 0;
    require(mincore(page, 4096, &result) == 0, "mincore");
    return result & 1;
}

int main(void) {
    const size_t first = 1u << 20, last = sizeof sparse - 4096;
    require(!resident((void *)&sparse[first]), "untouched BSS is not resident");
    require(sparse[first] == 0 && sparse[last] == 0, "zero BSS reads");
    sparse[first] = 17;
    require(resident((void *)&sparse[first]), "written BSS is resident");
    pid_t child = fork();
    require(child >= 0, "fork of sparse BSS");
    if (!child) {
        require(sparse[first] == 17 && sparse[last] == 0, "fork inherits bytes and zeros");
        sparse[first] = 42; sparse[last] = 99;
        require(sparse[first] == 42 && sparse[last] == 99, "child stores");
        _exit(0);
    }
    int status = 0;
    require(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status), "child exit");
    require(sparse[first] == 17 && sparse[last] == 0, "private COW");
    require(madvise((void *)&sparse[first], 4096, MADV_DONTNEED) == 0, "discard BSS");
    require(!resident((void *)&sparse[first]), "discard removes residency");
    require(sparse[first] == 0, "discard restores zeros");
    FILE *f = fopen("/proc/self/status", "r");
    require(f != NULL, "proc status");
    char line[256]; size_t rss = 0, size = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "VmRSS:", 6)) rss = strtoul(line + 6, NULL, 10);
        if (!strncmp(line, "VmSize:", 7)) size = strtoul(line + 7, NULL, 10);
    }
    fclose(f);
    require(size >= sizeof sparse / 1024, "virtual BSS remains mapped");
    require(rss > 0 && rss < 64u * 1024, "sparse BSS is not charged in full");
    printf("zero-bss bytes=%zu virtual_kib=%zu resident_kib=%zu COW/discard=PASS\n", sizeof sparse, size, rss);
    return 0;
}
