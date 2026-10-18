// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>
#ifndef SYS_SECCOMP
#define SYS_SECCOMP 1
#endif

static void install(unsigned action, unsigned flags) {
    struct sock_filter code[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_getppid, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, action),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog program = { .len = 4, .filter = code };
    assert(syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, flags, &program) == 0);
}
static volatile sig_atomic_t trapped;
static void trap_handler(int sig, siginfo_t *info, void *context) {
    assert(sig == SIGSYS && info->si_code == SYS_SECCOMP);
    assert(info->si_syscall == SYS_getppid && info->si_errno == 0x55);
    assert(info->si_call_addr != NULL);
    ucontext_t *uc = context;
#if defined(__aarch64__)
    assert(info->si_arch == AUDIT_ARCH_AARCH64);
    uc->uc_mcontext.regs[0] = 777;
#elif defined(__x86_64__)
    assert(info->si_arch == AUDIT_ARCH_X86_64);
    uc->uc_mcontext.gregs[REG_RAX] = 777;
#else
#error Unsupported oracle architecture
#endif
    trapped = 1;
}
static int channel[2];
static const char *stage = "fork/exec and filter inheritance";
static void *worker(void *unused) {
    (void)unused;
    char ch;
    assert(read(channel[0], &ch, 1) == 1);
    errno = 0;
    assert(syscall(SYS_getppid) == -1 && errno == 42);
    return NULL;
}
static void wait_ok(pid_t pid) {
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) fprintf(stderr,"seccomp %s: child status %#x\n",stage,status);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "exec-child")) {
        errno = 0;
        assert(syscall(SYS_getppid) == -1 && errno == 123);
        return 0;
    }
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
        install(SECCOMP_RET_ERRNO | 123, 0);
        assert(prctl(PR_GET_SECCOMP) == 2);
        errno = 0; assert(syscall(SYS_getppid) == -1 && errno == 123);
        assert(syscall(SYS_getpid) > 0);
        pid_t grandchild = fork(); assert(grandchild >= 0);
        if (!grandchild) { execl(argv[0], argv[0], "exec-child", NULL); _exit(99); }
        wait_ok(grandchild);
        install(SECCOMP_RET_ALLOW, 0);
        errno = 0; assert(syscall(SYS_getppid) == -1 && errno == 123);
        install(SECCOMP_RET_ERRNO | 42, 0);
        errno = 0; assert(syscall(SYS_getppid) == -1 && errno == 42);
        struct sigaction sa = {.sa_sigaction = trap_handler, .sa_flags = SA_SIGINFO};
        sigemptyset(&sa.sa_mask); assert(sigaction(SIGSYS, &sa, NULL) == 0);
        install(SECCOMP_RET_TRAP | 0x55, 0);
        assert(syscall(SYS_getppid) == 777 && trapped);
        _exit(0);
    }
    wait_ok(child);
    stage = "thread synchronization";
    child = fork(); assert(child >= 0);
    if (!child) {
        assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
        assert(pipe(channel) == 0);
        pthread_t thread; assert(pthread_create(&thread, NULL, worker, NULL) == 0);
        install(SECCOMP_RET_ERRNO | 42, SECCOMP_FILTER_FLAG_TSYNC);
        assert(write(channel[1], "x", 1) == 1);
        assert(pthread_join(thread, NULL) == 0);
        close(channel[0]); close(channel[1]); _exit(0);
    }
    wait_ok(child);
    child = fork(); assert(child >= 0);
    if (!child) {
        assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
        install(SECCOMP_RET_KILL_PROCESS, 0);
        syscall(SYS_getppid); _exit(99);
    }
    int status; assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSYS);
    puts("seccomp: errno, stacking, fork/exec, TRAP siginfo/context, TSYNC and KILL_PROCESS: ok");
    return 0;
}
