// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv) {
    (void)argv;
#ifndef STATIC_TEST
    void *library = dlopen("/work/libwork.so", RTLD_NOW);
    if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    int (*answer)(void) = (int (*)(void))dlsym(library, "answer");
    assert(answer && answer() == 42);
    assert(!dlclose(library));
#endif
    int fd = open("/work/mapping-data", argc > 1 ? O_RDONLY : O_RDWR | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    if (argc == 1) {
        assert(!ftruncate(fd, 8192));
        assert(pwrite(fd, "original", 8, 4096) == 8);
    }
    char *data = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 4096);
    assert(data != MAP_FAILED && !memcmp(data, "original", 8));
    memcpy(data, "private!", 8);
    char bytes[8];
    assert(pread(fd, bytes, sizeof(bytes), 4096) == sizeof(bytes));
    assert(!memcmp(bytes, "original", 8));
    assert(!msync(data, 4096, MS_SYNC));
    pid_t child = fork(); assert(child >= 0);
    if (!child) { data[0] = 'C'; _exit(data[0] == 'C' ? 0 : 1); }
    int status; assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0 && data[0] == 'p');
    assert(!munmap(data, 4096));
    child = fork(); assert(child >= 0);
    if (!child) {
        volatile char *beyond = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 8192);
        assert(beyond != MAP_FAILED);
        _exit(beyond[0] ? 2 : 3);
    }
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGBUS);
    assert(!close(fd));
    puts("WORKSPACE_OK");
    return 0;
}
