// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static void check(int ok, const char *what) {
    if (!ok) { fprintf(stderr, "storage: %s: %s\n", what, strerror(errno)); exit(1); }
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const off_t large = 20 * 1024 * 1024 + 17;
    int fd = open("file", O_CREAT | O_EXCL | O_RDWR, 0600);
    check(fd >= 0, "create");
    check(ftruncate(fd, large) == 0, "large sparse truncate");
    unsigned char zero[32], expected[32] = {0};
    check(pread(fd, zero, sizeof(zero), large - 100) == sizeof(zero) && !memcmp(zero, expected, sizeof(zero)), "sparse zeros");
    check(lseek(fd, 3, SEEK_SET) == 3, "seek");
    check(pwrite(fd, "\0XY", 3, large - 3) == 3 && lseek(fd, 0, SEEK_CUR) == 3, "positioned write");
    check(pread(fd, zero, 3, large - 3) == 3 && !memcmp(zero, "\0XY", 3), "positioned read");
    check(read(fd, zero, 1) == 1 && zero[0] == 0, "ordinary read offset");
    check(fsync(fd) == 0 && fdatasync(fd) == 0, "flush");
    struct stat st;
    check(fstat(fd, &st) == 0 && st.st_size == large && st.st_blocks < large / 512, "large sparse size and blocks");
    puts("large positioned IO and sparse zeros");
    check(link("file", "alias") == 0, "hard link");
    check(stat("alias", &st) == 0 && st.st_nlink == 2, "link count");
    check(chmod("alias", 0640) == 0 && fchmod(fd, 0600) == 0, "permissions");
    struct timespec times[2] = {{1234567, 123}, {7654321, 456}};
    check(utimensat(AT_FDCWD, "alias", times, 0) == 0, "timestamps");
    check(stat("file", &st) == 0 && (st.st_mode & 0777) == 0600 && st.st_atim.tv_sec == 1234567
          && st.st_atim.tv_nsec == 123 && st.st_mtim.tv_sec == 7654321 && st.st_mtim.tv_nsec == 456, "stat timestamps");
    check(rename("file", "renamed") == 0, "rename");
    check(unlink("renamed") == 0 && unlink("alias") == 0, "unlink aliases");
    check(fstat(fd, &st) == 0 && st.st_nlink == 0, "open unlink stat");
    check(pread(fd, zero, 3, large - 3) == 3 && zero[1] == 'X', "open unlink read");
    check(ftruncate(fd, 2) == 0 && fstat(fd, &st) == 0 && st.st_size == 2, "shrink unlinked");
    check(close(fd) == 0, "last close"); puts("hard links permissions timestamps and open unlink");
    fd = open("source", O_CREAT | O_RDWR, 0600); check(fd >= 0 && write(fd, "new", 3) == 3, "source");
    int old = open("target", O_CREAT | O_RDWR, 0600); check(old >= 0 && write(old, "old", 3) == 3, "target");
    check(rename("source", "target") == 0 && pread(old, zero, 3, 0) == 3 && !memcmp(zero, "old", 3), "replace keeps open inode");
    close(fd); close(old); check(truncate("target", 1) == 0, "path truncate");
    fd = open("target", O_RDONLY); check(fd >= 0, "readonly open"); errno = 0;
    check(ftruncate(fd, 0) == -1 && errno == EINVAL, "readonly truncate rejected"); close(fd);
    fd = open("append", O_CREAT | O_RDWR | O_APPEND, 0600); check(fd >= 0, "append open");
    check(write(fd, "a", 1) == 1 && pwrite(fd, "b", 1, 0) == 1 && lseek(fd, 0, SEEK_CUR) == 1, "Linux positioned append");
    check(pread(fd, zero, 2, 0) == 2 && !memcmp(zero, "ab", 2), "append bytes"); close(fd);
    puts("rename replacement truncate and append offsets");
    check(mkdir("old", 0700) == 0 && mkdir("old/sub", 0700) == 0, "directories");
    int dir = open("old/sub", O_DIRECTORY | O_RDONLY); check(dir >= 0, "directory descriptor");
    int ready[2], proceed[2]; check(pipe(ready) == 0 && pipe(proceed) == 0, "coordination pipes");
    pid_t child = fork(); check(child >= 0, "fork");
    if (child == 0) {
        check(chdir("old/sub") == 0 && write(ready[1], "r", 1) == 1 && read(proceed[0], zero, 1) == 1, "child coordination");
        char cwd[4096]; check(getcwd(cwd, sizeof(cwd)) != NULL, "renamed cwd");
        size_t n = strlen(cwd); check(n >= 8 && !strcmp(cwd + n - 8, "/new/sub"), "cwd follows rename");
        int f = openat(dir, "child", O_CREAT | O_WRONLY, 0600); check(f >= 0 && write(f, "child", 5) == 5, "renamed dirfd"); close(f); _exit(0);
    }
    check(read(ready[0], zero, 1) == 1 && rename("old", "new") == 0 && write(proceed[1], "p", 1) == 1, "parent rename");
    int status; check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0, "child cwd outcome");
    check(stat("new/sub/child", &st) == 0 && st.st_size == 5, "renamed child file"); close(dir);
    for (int i = 0; i < 2; i++) { close(ready[i]); close(proceed[i]); }
    puts("fork cwd and directory descriptor follow rename");
    struct statfs space; check(statfs(".", &space) == 0 && space.f_bsize > 0 && space.f_bavail <= space.f_blocks, "filesystem capacity");
    fd = open("target", O_RDONLY); check(fd >= 0 && fstatfs(fd, &space) == 0 && space.f_namelen == 255, "descriptor capacity"); close(fd);
    sync(); puts("capacity and sync"); return 0;
}
