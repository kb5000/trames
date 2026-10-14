// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc, char **argv) {
    const char *expected = getenv("CHECK_EXECFN");
    if (expected) assert(!strcmp((char *)getauxval(AT_EXECFN), expected));
    const char *closed = getenv("CHECK_CLOSED_FD");
    if (closed) { errno = 0; assert(fcntl(atoi(closed), F_GETFD) == -1 && errno == EBADF); }
    if (getenv("CHECK_DELETED")) {
        char target[4096];
        ssize_t n = readlink("/proc/self/exe", target, sizeof(target)-1); assert(n > 0);
        target[n] = 0; assert(strstr(target, " (deleted)"));
        int fd = open("/proc/self/exe", O_RDONLY); assert(fd >= 0);
        char magic[4]; assert(read(fd, magic, 4) == 4 && !memcmp(magic, "\177ELF", 4));
        struct stat st; assert(fstat(fd, &st) == 0 && st.st_nlink == 0); close(fd);
    }
    if (getenv("CHECK_REEXEC") && argc == 1) {
        unsetenv("CHECK_EXECFN");
        char *next[] = { argv[0], "again", NULL };
        execv("/proc/self/exe", next); assert(0);
    }
    puts("Hello from ARM64 syscalls");
    return 0;
}
