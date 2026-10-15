// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

static void named(const char *name) {
    struct utsname u;
    assert(uname(&u) == 0 && !strcmp(u.nodename, name));
}
static void waited(pid_t child) {
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static int clone_child(void *unused) {
    (void)unused;
    assert(sethostname("clone-child", 11) == 0);
    named("clone-child");
    return 0;
}
static void copy_self(const char *target) {
    int in = open("/proc/self/exe", O_RDONLY), out = open(target, O_CREAT | O_WRONLY | O_TRUNC, 0755);
    assert(in >= 0 && out >= 0);
    char buffer[8192];
    ssize_t n;
    while ((n = read(in, buffer, sizeof buffer)) > 0) assert(write(out, buffer, n) == n);
    assert(n == 0); close(in); close(out);
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "jailed")) {
        char cwd[256];
        assert(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/"));
        assert(access("/program", X_OK) == 0);
        assert(access("/../outside", F_OK) == -1 && errno == ENOENT);
        int fd = open("/absolute-link", O_RDONLY); assert(fd >= 0);
        char value[4] = {0}; assert(read(fd, value, 3) == 3 && !strcmp(value, "yes"));
        close(fd); return 0;
    }
    assert(sethostname("parent", 6) == 0);
    int original = open("/proc/self/ns/uts", O_RDONLY | O_CLOEXEC); assert(original >= 0);
    struct stat before, after;
    assert(fstat(original, &before) == 0 && S_ISREG(before.st_mode));
    struct statfs fs;
    assert(fstatfs(original, &fs) == 0 && fs.f_type == 0x6e736673);
    assert(ioctl(original, 0xb703) == CLONE_NEWUTS);
    assert(unshare(CLONE_NEWUTS | CLONE_NEWUSER) == -1 && errno == EINVAL);
    assert(stat("/proc/self/ns/uts", &after) == 0 && before.st_ino == after.st_ino);
    pid_t child = fork(); assert(child >= 0);
    if (!child) { assert(sethostname("shared", 6) == 0); _exit(0); }
    waited(child); named("shared");
    assert(unshare(CLONE_NEWUTS | CLONE_FS | CLONE_FILES) == 0);
    assert(sethostname("private", 7) == 0 && setdomainname("example", 7) == 0);
    struct utsname u; assert(uname(&u) == 0 && !strcmp(u.domainname, "example"));
    assert(stat("/proc/self/ns/uts", &after) == 0 && before.st_ino != after.st_ino);
    assert(setns(original, CLONE_NEWNS) == -1 && errno == EINVAL);
    assert(setns(original, CLONE_NEWUTS) == 0); named("shared");
    int alias = dup(original); assert(alias >= 0); close(original);
    assert(unshare(CLONE_NEWUTS) == 0 && setns(alias, 0) == 0); close(alias);
    char *stack = malloc(65536); assert(stack);
    child = clone(clone_child, stack + 65536, CLONE_NEWUTS | SIGCHLD, NULL); assert(child > 0);
    waited(child); free(stack); named("shared");
    int ready[2], release[2]; assert(pipe(ready) == 0 && pipe(release) == 0);
    child = fork(); assert(child >= 0);
    if (!child) {
        assert(unshare(CLONE_NEWUTS) == 0 && sethostname("retained", 8) == 0);
        assert(write(ready[1], "x", 1) == 1);
        char b; assert(read(release[0], &b, 1) == 1); _exit(0);
    }
    char b; assert(read(ready[0], &b, 1) == 1);
    char path[128]; snprintf(path, sizeof path, "/proc/%d/ns/uts", child);
    int retained = open(path, O_RDONLY); assert(retained >= 0);
    assert(write(release[1], "x", 1) == 1); waited(child);
    assert(setns(retained, 0) == 0); named("retained"); close(retained);
    close(ready[0]); close(ready[1]); close(release[0]); close(release[1]);
    puts("UTS: fork sharing, clone/unshare isolation, namespace fd/stat/ioctl/setns/lifetime: ok");

    assert(mkdir("/jail", 0755) == 0);
    copy_self("/jail/program");
    int f = open("/outside", O_CREAT | O_WRONLY, 0600); assert(f >= 0); close(f);
    f = open("/jail/inside", O_CREAT | O_WRONLY, 0644); assert(f >= 0 && write(f, "yes", 3) == 3); close(f);
    assert(symlink("/inside", "/jail/absolute-link") == 0);
    child = fork(); assert(child >= 0);
    if (!child) {
        assert(chroot("/jail") == 0 && chdir("/") == 0);
        execl("/program", "program", "jailed", NULL); _exit(2);
    }
    waited(child);
    assert(access("/outside", F_OK) == 0);
    child = fork(); assert(child >= 0);
    if (!child) {
        assert(setuid(1000) == 0);
        assert(unshare(CLONE_NEWUTS) == -1 && errno == EPERM);
        assert(sethostname("no", 2) == -1 && errno == EPERM);
        assert(chroot("/jail") == -1 && errno == EPERM); _exit(0);
    }
    waited(child);
    puts("chroot: independent fs root, absolute symlinks, dotdot, exec, privilege: ok");
    return 0;
}
