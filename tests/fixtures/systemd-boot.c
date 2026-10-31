// SPDX-License-Identifier: GPL-3.0-or-later
// Guest-only container setup. Compile on Linux; execute only inside Trames.
#define _GNU_SOURCE
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void fail(const char *operation) {
    perror(operation);
    exit(1);
}

static void bind_console(const char *source, const char *destination) {
    int fd = open(destination, O_CREAT | O_RDWR, 0600);
    if (fd < 0) fail(destination);
    if (close(fd) < 0) fail("close console");
    if (mount(source, destination, NULL, MS_BIND, NULL) < 0) fail("bind console");
}

int main(void) {
    struct utsname uts;
    if (getpid() != 1 || uname(&uts) < 0 || !strstr(uts.release, "trames")) {
        fprintf(stderr, "systemd-boot requires Trames guest PID 1\n");
        return 1;
    }
    if (mount("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755") < 0)
        fail("mount run");
    bind_console("/dev/pts/0", "/run/console");
    if (mount("tmpfs", "/dev", "tmpfs", MS_NOSUID, "mode=755") < 0)
        fail("mount dev");
    const char *names[] = {"null", "zero", "full", "random", "urandom", "tty"};
    unsigned minors[] = {3, 5, 7, 8, 9, 0};
    for (int i = 0; i < 6; i++) {
        char path[40];
        snprintf(path, sizeof(path), "/dev/%s", names[i]);
        if (mknod(path, S_IFCHR | 0666, makedev(i == 5 ? 5 : 1, minors[i])) < 0)
            fail(path);
    }
    bind_console("/run/console", "/dev/console");
    if (mkdir("/dev/pts", 0755) < 0 || mkdir("/dev/shm", 01777) < 0)
        fail("mkdir dev mounts");
    if (mount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC,
              "newinstance,mode=620,ptmxmode=666") < 0)
        fail("mount pts");
    if (symlink("pts/ptmx", "/dev/ptmx") < 0 || symlink("/proc/self/fd", "/dev/fd") < 0)
        fail("symlink dev");
    if (mount("tmpfs", "/dev/shm", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777") < 0)
        fail("mount shm");
    if (setenv("container", "trames", 1) < 0 || setenv("SYSTEMD_LOG_LEVEL", "debug", 1) < 0 ||
        setenv("SYSTEMD_LOG_TARGET", "console", 1) < 0)
        fail("setenv");
    execl("/usr/lib/systemd/systemd", "systemd", "--system", "--unit=basic.target", NULL);
    fail("exec systemd");
}
