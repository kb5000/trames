// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static int rootns, oldproc, ready[2], resume_pipe[2], orphan_pipe[2];
static char stack1[65536] __attribute__((aligned(16)));
static char stack2[65536] __attribute__((aligned(16)));
static void waited(pid_t p) { int s; assert(waitpid(p, &s, 0) == p && WIFEXITED(s) && WEXITSTATUS(s) == 0); }
static void byte_write(int fd) { assert(write(fd, "x", 1) == 1); }
static void byte_read(int fd) { char b; assert(read(fd, &b, 1) == 1); }
static void read_file(const char *path, char *buf, size_t size) { int f = open(path, O_RDONLY); assert(f >= 0); ssize_t n = read(f, buf, size-1); assert(n > 0); buf[n] = 0; close(f); }
static void signal_handler(int sig) { (void)sig; }
static void mount_proc(void) { assert(mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0); }
static void *thread(void *unused) {
    if (unused) {
        char expected[32]; snprintf(expected, sizeof expected, "%d", getpid());
        char *args[] = {"pid-namespaces", "exec-check", expected, NULL};
        execv("/proc/self/exe", args); assert(0);
    }
    assert(getpid() == 1 && syscall(SYS_gettid) > 1);
    assert(unshare(CLONE_NEWPID) == -1 && errno == EINVAL);
    assert(syscall(SYS_tgkill, 1, syscall(SYS_gettid), 0) == 0);
    char b[128], expected[128];
    ssize_t n = readlink("/proc/thread-self", b, sizeof b - 1); assert(n > 0); b[n] = 0;
    snprintf(expected, sizeof expected, "1/task/%ld", syscall(SYS_gettid)); assert(!strcmp(b, expected));
    return NULL;
}
static int nested(void *unused) {
    (void)unused;
    assert(getpid() == 1 && getppid() == 0 && getpgrp() == 1 && getsid(0) == 1);
    char before[128]; ssize_t n = readlink("/proc/self", before, sizeof before-1); assert(n > 0); before[n] = 0;
    assert(atoi(before) > 1); // inherited proc belongs to parent namespace
    assert(unshare(CLONE_NEWNS) == 0); mount_proc();
    char b[4096]; read_file("/proc/self/status", b, sizeof b);
    assert(strstr(b, "Pid:\t1\n") && strstr(b, "PPid:\t0\n") && strstr(b, "NSpid:\t1\n"));
    assert(kill(2, 0) == -1 && errno == ESRCH);
    pid_t p = fork(); assert(p >= 0);
    if (!p) { assert(getpid() == 2 && getppid() == 1); _exit(0); }
    assert(p == 2); waited(p);
    return 0;
}
static int isolated(void *unused) {
    (void)unused;
    assert(getpid() == 1 && syscall(SYS_gettid) == 1 && getppid() == 0);
    char comm[16]; assert(prctl(PR_SET_NAME, "pid-init") == 0 && prctl(PR_GET_NAME, comm) == 0 && !strcmp(comm, "pid-init"));
    assert(getpgrp() == 1 && getsid(0) == 1);
    assert(setns(rootns, CLONE_NEWPID) == -1 && errno == EINVAL);
    assert(setpgid(0, -1) == -1 && errno == EINVAL);
    assert(syscall(SYS_ioprio_get, 1, -1) == -1 && errno == EINVAL);
    assert(syscall(SYS_ioprio_get, 2, 999) == -1 && errno == EOPNOTSUPP);
    assert(kill(2, 0) == -1 && errno == ESRCH);
    char b[8192], old[128];
    ssize_t n = readlinkat(oldproc, "self", old, sizeof old-1); assert(n > 0); old[n] = 0;
    assert(atoi(old) > 1);
    assert(unshare(CLONE_NEWNS) == 0); mount_proc();
    read_file("/proc/1/status", b, sizeof b);
    assert(strstr(b, "NSpid:\t1\n") && strstr(b, "PPid:\t0\n") && strstr(b, "Name:\tpid-init\n"));
    assert(access("/proc/2/status", F_OK) == -1 && errno == ENOENT);
    int own = open("/proc/self/ns/pid", O_RDONLY); assert(own >= 0 && ioctl(own, 0xb703) == CLONE_NEWPID); close(own);
    DIR *d = opendir("/proc"); assert(d); struct dirent *de; int count = 0;
    while ((de = readdir(d))) if (de->d_name[0] >= '0' && de->d_name[0] <= '9') { assert(!strcmp(de->d_name, "1")); count++; }
    closedir(d); assert(count == 1);
    sigset_t mask; sigemptyset(&mask); sigaddset(&mask, SIGUSR1);
    assert(signal(SIGUSR1, signal_handler) != SIG_ERR && sigprocmask(SIG_BLOCK, &mask, NULL) == 0);
    pid_t p = fork(); assert(p >= 0);
    if (!p) { assert(getpid() == 2 && getppid() == 1); assert(kill(1, SIGUSR1) == 0); _exit(7); }
    assert(p == 2);
    siginfo_t si; assert(sigwaitinfo(&mask, &si) == SIGUSR1 && si.si_pid == 2);
    assert(waitid(P_PID, p, &si, WEXITED | WNOWAIT) == 0 && si.si_pid == p && si.si_code == CLD_EXITED && si.si_status == 7);
    int s; assert(waitpid(p, &s, 0) == p && WEXITSTATUS(s) == 7);
    assert(sigprocmask(SIG_UNBLOCK, &mask, NULL) == 0);
    // Namespace init ignores unhandled signals from its own namespace.
    assert(kill(1, SIGTERM) == 0 && kill(1, SIGKILL) == 0);
    struct itimerval alarm_timer = {.it_value = {.tv_usec = 1000}};
    assert(setitimer(ITIMER_REAL, &alarm_timer, NULL) == 0);
    assert(usleep(10000) == 0); // default timer signal cannot kill namespace init
    pthread_t t; assert(pthread_create(&t, NULL, thread, NULL) == 0 && pthread_join(t, NULL) == 0);
    p = fork(); assert(p >= 0);
    if (!p) {
        assert(pthread_create(&t, NULL, thread, (void *)1) == 0);
        for (;;) pause();
    }
    waited(p);
    p = clone(nested, stack2 + sizeof stack2, CLONE_NEWPID | SIGCHLD, NULL); assert(p > 1); waited(p);
    // A retained ancestor proc mount continues to report ancestor IDs.
    char now[128]; n = readlinkat(oldproc, "self", now, sizeof now-1); assert(n > 0); now[n] = 0; assert(!strcmp(now, old));
    // Orphan adoption by namespace init, then waitid(P_ALL).
    int pipefd[2]; assert(pipe(pipefd) == 0);
    p = fork(); assert(p >= 0);
    if (!p) {
        pid_t c = fork(); assert(c >= 0);
        if (!c) { byte_read(pipefd[0]); assert(getppid() == 1); _exit(23); }
        _exit(0);
    }
    waited(p); byte_write(pipefd[1]);
    assert(waitid(P_ALL, 0, &si, WEXITED) == 0 && si.si_pid > p && si.si_status == 23);
    close(pipefd[0]); close(pipefd[1]);
    // Leave a blocked descendant for PID 1 shutdown to kill.
    p = fork(); assert(p >= 0);
    if (!p) { close(ready[1]); close(resume_pipe[1]); byte_read(orphan_pipe[0]); _exit(99); }
    close(orphan_pipe[1]);
    sigemptyset(&mask); sigaddset(&mask, SIGUSR2);
    assert(signal(SIGUSR2, signal_handler) != SIG_ERR && sigprocmask(SIG_BLOCK, &mask, NULL) == 0);
    byte_write(ready[1]);
    assert(sigwaitinfo(&mask, &si) == SIGUSR2 && si.si_pid == 0);
    byte_write(ready[1]); byte_read(resume_pipe[0]);
    return 0;
}
static void unshare_children(void) {
    pid_t own = getpid(); assert(unshare(CLONE_NEWPID) == 0 && getpid() == own);
    assert(unshare(CLONE_NEWPID) == -1 && errno == EINVAL);
    int fd = open("/proc/self/ns/pid_for_children", O_RDONLY); assert(fd >= 0);
    pid_t p = fork(); assert(p >= 0);
    if (!p) { assert(getpid() == 1 && getppid() == 0); _exit(0); }
    waited(p); assert(fork() == -1 && errno == ENOMEM);
    assert(setns(rootns, CLONE_NEWPID) == 0); p = fork(); assert(p >= 0);
    if (!p) { assert(getpid() != 1 && getppid() == own); _exit(0); }
    waited(p); assert(setns(fd, 0) == 0 && fork() == -1 && errno == ENOMEM); close(fd);
}
static void subreaper(void) {
    int value; assert(prctl(PR_SET_CHILD_SUBREAPER, 1) == 0);
    assert(prctl(PR_GET_CHILD_SUBREAPER, &value) == 0 && value == 1);
    int fds[2]; assert(pipe(fds) == 0); pid_t own = getpid();
    pid_t p = fork(); assert(p >= 0);
    if (!p) {
        assert(prctl(PR_GET_CHILD_SUBREAPER, &value) == 0 && value == 0);
        pid_t c = fork(); assert(c >= 0);
        if (!c) { byte_read(fds[0]); assert(getppid() == own); _exit(0); }
        _exit(0);
    }
    waited(p); byte_write(fds[1]); int s; assert(wait(&s) > p && WIFEXITED(s) && !WEXITSTATUS(s));
    close(fds[0]); close(fds[1]);
}
static int killed_init(void *unused) {
    (void)unused; assert(getpid() == 1);
    byte_write(ready[1]); byte_read(resume_pipe[0]); return 99;
}
int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "exec-check")) {
        assert(getpid() == atoi(argv[2]) && syscall(SYS_gettid) == getpid() && getppid() == 1);
        char b[128]; ssize_t n = readlink("/proc/self", b, sizeof b-1); assert(n > 0); b[n] = 0;
        assert(atoi(b) == getpid()); return 0;
    }
    rootns = open("/proc/self/ns/pid", O_RDONLY); oldproc = open("/proc", O_RDONLY | O_DIRECTORY); assert(rootns >= 0 && oldproc >= 0);
    assert(pipe(ready) == 0 && pipe(resume_pipe) == 0 && pipe(orphan_pipe) == 0);
    pid_t p = clone(isolated, stack1 + sizeof stack1, CLONE_NEWPID | SIGCHLD, NULL); assert(p > 1);
    close(orphan_pipe[1]); byte_read(ready[0]);
    char path[128]; snprintf(path, sizeof path, "/proc/%d/ns/pid", p); int fd = open(path, O_RDONLY); assert(fd >= 0);
    // Ancestors see both IDs; proc status includes the namespace hierarchy.
    snprintf(path, sizeof path, "/proc/%d/status", p); char status[8192], expected[128]; read_file(path, status, sizeof status);
    snprintf(expected, sizeof expected, "NSpid:\t%d\t1\n", p); assert(strstr(status, expected));
    siginfo_t si; memset(&si, 0xa5, sizeof si);
    assert(waitid(P_PID, p, &si, WEXITED | WNOHANG) == 0 && si.si_pid == 0 && si.si_signo == 0);
    assert(kill(p, SIGSTOP) == 0);
    assert(waitid(P_PID, p, &si, WSTOPPED) == 0 && si.si_pid == p && si.si_status == SIGSTOP);
    assert(kill(p, SIGCONT) == 0);
    assert(waitid(P_PID, p, &si, WCONTINUED) == 0 && si.si_status == SIGCONT);
    assert(kill(p, SIGUSR2) == 0); byte_read(ready[0]);
    pid_t joiner = fork(); assert(joiner >= 0);
    if (!joiner) {
        assert(setns(fd, CLONE_NEWPID) == 0);
        pid_t c = fork(); assert(c >= 0);
        if (!c) { assert(getpid() > 1 && getppid() == 0); _exit(0); }
        waited(c); _exit(0);
    }
    waited(joiner);
    byte_write(resume_pipe[1]); waited(p);
    char b; assert(read(orphan_pipe[0], &b, 1) == 0);
    pid_t c = fork(); assert(c >= 0);
    if (!c) { assert(setns(fd, CLONE_NEWPID) == 0 && fork() == -1 && errno == ENOMEM); _exit(0); }
    waited(c); close(fd);
    puts("pid: clone hierarchy, proc mount views, signals/waitid, threads, orphan adoption and init shutdown: ok");
    p = fork(); assert(p >= 0); if (!p) { unshare_children(); _exit(0); } waited(p);
    p = fork(); assert(p >= 0); if (!p) { subreaper(); _exit(0); } waited(p);
    assert(clone(nested, stack1 + sizeof stack1, CLONE_NEWPID | CLONE_PARENT | SIGCHLD, NULL) == -1 && errno == EINVAL);
    int parent_tid = -1, child_tid = -1;
    p = syscall(SYS_clone, CLONE_NEWPID | CLONE_PARENT_SETTID | CLONE_CHILD_SETTID | SIGCHLD, 0, &parent_tid, 0, &child_tid);
    assert(p >= 0);
    if (!p) { assert(child_tid == 1 && getpid() == 1); _exit(0); }
    assert(parent_tid == p && child_tid == -1); waited(p);
    p = clone(killed_init, stack1 + sizeof stack1, CLONE_NEWPID | SIGCHLD, NULL); assert(p > 1);
    byte_read(ready[0]); assert(kill(p, SIGKILL) == 0);
    int st; assert(waitpid(p, &st, 0) == p && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
    puts("pid: unshare/setns child-only semantics, dead namespace rejection and subreaper: ok");
    return 0;
}
