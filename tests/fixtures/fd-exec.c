// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
static unsigned char *image;
static size_t image_size;
static int memfd(const char *name, unsigned flags) {
    int fd = memfd_create(name, flags); assert(fd >= 0);
    assert(write(fd, image, image_size) == (ssize_t)image_size);
    return fd;
}
static void execute(const char *label, int at, int fd, const char *path, int flags,
                    int expected_errno, int closed, int deleted, int again, const char *wanted) {
    int output[2]; assert(pipe(output) == 0);
    pid_t child = fork(); assert(child >= 0);
    if (child == 0) {
        close(output[0]); assert(dup2(output[1], 1) == 1); close(output[1]);
        char execfn[4096], number[32];
        if (at && (path[0] == 0 || (path[0] != '/' && fd != AT_FDCWD)))
            snprintf(execfn, sizeof execfn, path[0] ? "/dev/fd/%d/%s" : "/dev/fd/%d", fd, path);
        else snprintf(execfn, sizeof execfn, "%s", path);
        assert(setenv("CHECK_EXECFN", execfn, 1) == 0);
        snprintf(number, sizeof number, "%d", closed);
        if (closed >= 0) assert(setenv("CHECK_CLOSED_FD", number, 1) == 0);
        if (deleted) assert(setenv("CHECK_DELETED", "1", 1) == 0);
        if (again) assert(setenv("CHECK_REEXEC", "1", 1) == 0);
        char *argv[] = { "deliberately-different-argv0", NULL };
        if (at == 2) fexecve(fd, argv, environ);
        else if (at) syscall(SYS_execveat, fd, path, argv, environ, flags);
        else execve(path, argv, environ);
        if (errno != expected_errno) { fprintf(stderr, "%s errno=%d expected=%d\n", label, errno, expected_errno); _exit(99); }
        _exit(expected_errno ? 0 : 98);
    }
    close(output[1]); char buffer[512]; size_t used = 0; ssize_t n;
    while ((n = read(output[0], buffer + used, sizeof(buffer)-used-1)) > 0) used += (size_t)n;
    assert(n == 0); buffer[used] = 0; close(output[0]);
    int status; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(!strcmp(buffer, wanted)); printf("%s: ok\n", label);
}
int main(int argc, char **argv) {
    assert(argc == 2); int source = open(argv[1], O_RDONLY); assert(source >= 0);
    struct stat st; assert(fstat(source, &st) == 0 && st.st_size > 0);
    image_size = (size_t)st.st_size; image = malloc(image_size); assert(image);
    assert(read(source, image, image_size) == (ssize_t)image_size); close(source);
    int fd = memfd("penguin_elf", 0); char path[128], target[128];
    snprintf(path, sizeof path, "/proc/self/fd/%d", fd);
    ssize_t n = readlink(path, target, sizeof(target)-1); assert(n > 0); target[n] = 0;
    assert(!strcmp(target, "/memfd:penguin_elf (deleted)"));
    execute("proc memfd at EOF offset", 0, AT_FDCWD, path, 0, 0, -1, 1, 0, "Hello from ARM64 syscalls\n");
    execute("execveat memfd empty path", 1, fd, "", AT_EMPTY_PATH, 0, -1, 1, 0, "Hello from ARM64 syscalls\n");
    execute("original libc fexecve", 2, fd, "", AT_EMPTY_PATH, 0, -1, 1, 0, "Hello from ARM64 syscalls\n");
    execute("nofollow rejects proc magic link", 1, AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, ELOOP, -1, 0, 0, "");
    assert(fchmod(fd, 0600) == 0);
    execute("fd execute permission is required", 1, fd, "", AT_EMPTY_PATH, EACCES, -1, 0, 0, "");
    assert(fchmod(fd, 0700) == 0);
    assert(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
    execute("CLOEXEC ELF and proc exe inode pin", 1, fd, "", AT_EMPTY_PATH, 0, fd, 1, 1, "Hello from ARM64 syscalls\n");
    assert(fcntl(fd, F_GETFD) == FD_CLOEXEC); // child exec never closes parent's fd
    close(fd);
    char directory[] = "/tmp/fd-exec-XXXXXX"; assert(mkdtemp(directory));
    int dir = open(directory, O_PATH | O_DIRECTORY); assert(dir >= 0);
    execute("directory fd is not executable", 1, dir, "", AT_EMPTY_PATH, EACCES, -1, 0, 0, "");
    execute("cwd empty path is a directory", 1, AT_FDCWD, "", AT_EMPTY_PATH, EACCES, -1, 0, 0, "");
    char named[4096], alias[4096]; snprintf(named, sizeof named, "%s/hello", directory);
    int writer = open(named, O_WRONLY | O_CREAT | O_EXCL, 0700); assert(writer >= 0);
    assert(write(writer, image, image_size) == (ssize_t)image_size); close(writer);
    execute("relative dirfd ELF", 1, dir, "hello", 0, 0, -1, 0, 0, "Hello from ARM64 syscalls\n");
    execute("absolute path ignores invalid dirfd", 1, -99, named, 0, 0, -1, 0, 0, "Hello from ARM64 syscalls\n");
    snprintf(alias, sizeof alias, "%s/alias", directory); assert(symlink("hello", alias) == 0);
    execute("nofollow rejects final symlink", 1, dir, "alias", AT_SYMLINK_NOFOLLOW, ELOOP, -1, 0, 0, "");
    int named_fd = open(named, O_PATH | O_CLOEXEC); assert(named_fd >= 0);
    errno = 0; assert(read(named_fd, target, 1) == -1 && errno == EBADF);
    // musl intentionally falls back to proc-fd paths after raw EBADF; glibc
    // does not. Compare the kernel ABI here, without conflating libc policy.
    assert(syscall(SYS_fchmod, named_fd, 0600) == -1 && errno == EBADF);
    assert(syscall(SYS_fchown, named_fd, (uid_t)-1, (gid_t)-1) == -1 && errno == EBADF);
    assert(syscall(SYS_fchownat, named_fd, "", (uid_t)-1, (gid_t)-1, AT_EMPTY_PATH) == 0);
    assert(ftruncate(named_fd, 0) == -1 && errno == EBADF);
    assert(mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, named_fd, 0) == MAP_FAILED && errno == EBADF);
    execute("O_PATH ELF fd", 1, named_fd, "", AT_EMPTY_PATH, 0, named_fd, 0, 0, "Hello from ARM64 syscalls\n");
    assert(unlink(named) == 0);
    snprintf(path, sizeof path, "/proc/self/fd/%d", named_fd);
    execute("unlinked regular ELF through proc fd", 0, AT_FDCWD, path, 0, 0, named_fd, 1, 0, "Hello from ARM64 syscalls\n");
    assert(unlink(alias) == 0); assert(symlink(path, alias) == 0);
    execute("symlink to deleted inode magic link", 0, AT_FDCWD, alias, 0, 0, named_fd, 1, 0, "Hello from ARM64 syscalls\n");
    execute("invalid execveat flag", 1, named_fd, "", AT_EMPTY_PATH | 0x2000, EINVAL, -1, 0, 0, "");
    execute("empty path precedes unknown flags", 1, named_fd, "", 0x2000, ENOENT, -1, 0, 0, "");
    execute("empty pathname without flag", 1, named_fd, "", 0, ENOENT, -1, 0, 0, "");
    execute("invalid fd with empty path", 1, -9, "", AT_EMPTY_PATH, EBADF, -1, 0, 0, "");
    execute("regular fd cannot be a dirfd", 1, named_fd, "child", 0, ENOTDIR, -1, 0, 0, "");
    close(named_fd); unlink(alias);
    fd = memfd_create("script", 0); assert(fd >= 0);
    const char script[] = "#!/bin/sh\nprintf 'script-via-fd\\n'\n";
    assert(write(fd, script, sizeof(script)-1) == (ssize_t)sizeof(script)-1);
    execute("fd shebang interpreter", 1, fd, "", AT_EMPTY_PATH, 0, -1, 0, 0, "script-via-fd\n");
    assert(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
    execute("CLOEXEC script returns ENOENT", 1, fd, "", AT_EMPTY_PATH, ENOENT, -1, 0, 0, "");
    assert(fcntl(fd, F_GETFD) == FD_CLOEXEC); close(fd);
    snprintf(named, sizeof named, "%s/script", directory);
    writer = open(named, O_WRONLY | O_CREAT | O_EXCL, 0700); assert(writer >= 0);
    assert(write(writer, script, sizeof(script)-1) == (ssize_t)sizeof(script)-1); close(writer);
    execute("relative dirfd shebang", 1, dir, "script", 0, 0, -1, 0, 0, "script-via-fd\n");
    assert(fcntl(dir, F_SETFD, FD_CLOEXEC) == 0);
    execute("CLOEXEC dirfd script returns ENOENT", 1, dir, "script", 0, ENOENT, -1, 0, 0, "");
    unlink(named); close(dir); rmdir(directory); free(image); return 0;
}
