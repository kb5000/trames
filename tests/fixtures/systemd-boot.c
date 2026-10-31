// SPDX-License-Identifier: GPL-3.0-or-later
// Guest-only container setup. Compile on Linux; execute only inside Trames.
#define _GNU_SOURCE
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <linux/capability.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void fail(const char *operation) {
    perror(operation);
    int fd = open("/run/trames-controller.log", O_RDONLY);
    if (fd >= 0) {
        char text[8192]; ssize_t n = read(fd, text, sizeof(text));
        if (n > 0) { ssize_t written = write(2, text, n); (void) written; }
        close(fd);
    }
    exit(1);
}

static void bind_console(const char *source, const char *destination) {
    int fd = open(destination, O_CREAT | O_RDWR, 0600);
    if (fd < 0) fail(destination);
    if (close(fd) < 0) fail("close console");
    if (mount(source, destination, NULL, MS_BIND, NULL) < 0) fail("bind console");
}

static int command(char *const args[], const char *output) {
    pid_t child = fork();
    if (child < 0) fail("fork command");
    if (child == 0) {
        if (setenv("SYSTEMD_LOG_LEVEL", "warning", 1) < 0) fail("command log level");
        int fd = open(output, O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (fd < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0) fail("capture command");
        close(fd);
        execv(args[0], args); fail("exec command");
    }
    int status;
    if (waitpid(child, &status, 0) != child) fail("wait command");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 255;
}

static void exercise(void) {
    alarm(25);
    char *ready[] = {"/usr/bin/systemctl", "is-active", "basic.target", NULL};
    int active = 0;
    for (int i = 0; i < 100; i++) {
        if (command(ready, "/run/trames-controller.log") == 0) { active = 1; break; }
        usleep(100000);
    }
    if (!active) fail("basic.target readiness");
    char *start[] = {"/usr/bin/systemctl", "start", "trames-oneshot.service", "trames-notify.service", NULL};
    if (command(start, "/run/trames-controller.log") != 0) fail("start services");
    char *status[] = {"/usr/bin/systemctl", "is-active", "trames-oneshot.service", "trames-notify.service", NULL};
    if (command(status, "/run/trames-controller.log") != 0) fail("service states");
    char *journal[] = {"/usr/bin/journalctl", "--no-pager", "-u", "trames-oneshot.service", "-o", "cat", "-n", "20", NULL};
    if (command(journal, "/run/trames-journal-check.log") != 0) fail("journalctl");
    int fd = open("/run/trames-journal-check.log", O_RDONLY);
    char text[8192]; ssize_t length = fd >= 0 ? read(fd, text, sizeof(text) - 1) : -1;
    if (length < 0) fail("read journal result");
    text[length] = 0; close(fd);
    if (!strstr(text, "TRAMES_SYSTEMD_SERVICE_OK")) { fputs(text, stderr); fail("journal message missing"); }
    char *stop[] = {"/usr/bin/systemctl", "stop", "trames-notify.service", NULL};
    if (command(stop, "/run/trames-controller.log") != 0) fail("stop service");
    char *failed[] = {"/usr/bin/systemctl", "--no-legend", "--plain", "--state=failed", "list-units", NULL};
    if (command(failed, "/run/trames-controller.log") != 0) fail("query failed units");
    fd = open("/run/trames-controller.log", O_RDONLY);
    length = fd >= 0 ? read(fd, text, sizeof(text) - 1) : -1;
    if (length != 0) fail("unexpected failed units");
    close(fd);
    puts("TRAMES_SYSTEMD_SERVICE_AND_JOURNAL_OK"); fflush(stdout);
    if (kill(1, SIGRTMIN + 3) < 0) fail("request guest shutdown");
    _exit(0);
}

static void unit(const char *path, const char *binary, const char *mode, const char *type) {
    FILE *file = fopen(path, "w");
    if (!file) fail("create test unit");
    if (fprintf(file, "[Unit]\nDescription=Trames original systemd acceptance\n[Service]\nType=%s\nExecStart=%s %s\nRemainAfterExit=%s\nStandardOutput=journal\nTimeoutStopSec=2s\n", type, binary, mode, strcmp(type, "oneshot") == 0 ? "yes" : "no") < 0 || fclose(file) < 0)
        fail("write test unit");
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--oneshot") == 0) {
        puts("TRAMES_SYSTEMD_SERVICE_OK"); return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--notify") == 0) {
        const char *path = getenv("NOTIFY_SOCKET");
        if (!path) fail("NOTIFY_SOCKET");
        struct sockaddr_un address = {.sun_family = AF_UNIX};
        if (strlen(path) >= sizeof(address.sun_path)) fail("notify path length");
        strcpy(address.sun_path, path);
        int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0 || sendto(fd, "READY=1", 7, 0, (void *)&address, sizeof(address)) != 7) fail("send ready");
        close(fd);
        for (;;) pause();
    }
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
    if (mount(NULL, "/sys", NULL, MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0)
        fail("read-only sysfs");
    if (setenv("container", "trames", 1) < 0 || setenv("SYSTEMD_LOG_LEVEL", "debug", 1) < 0 ||
        setenv("SYSTEMD_LOG_TARGET", "console", 1) < 0)
        fail("setenv");
    // This userspace container has no kernel module loader. Preserve the real
    // capability semantics and let stock units skip their module-only work.
    if (prctl(PR_CAPBSET_DROP, CAP_SYS_MODULE, 0, 0, 0) < 0) fail("drop module capability");
    if (argc == 2 && strcmp(argv[1], "--exercise") == 0) {
        if (mkdir("/run/systemd", 0755) < 0 || mkdir("/run/systemd/system", 0755) < 0) fail("test unit directory");
        unit("/run/systemd/system/trames-oneshot.service", argv[0], "--oneshot", "oneshot");
        unit("/run/systemd/system/trames-notify.service", argv[0], "--notify", "notify");
        pid_t child = fork(); if (child < 0) fail("fork controller");
        if (child == 0) exercise();
    }
    execl("/usr/lib/systemd/systemd", "systemd", "--system", "--unit=basic.target", NULL);
    fail("exec systemd");
}
