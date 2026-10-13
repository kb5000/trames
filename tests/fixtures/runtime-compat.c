// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s errno=%d\n",__LINE__,#x,errno); exit(1); } } while (0)
static unsigned char alt[65536] __attribute__((aligned(16)));
static volatile sig_atomic_t signals, nested, fsize_signals, autodisarm;
static void handler(int sig, siginfo_t *info, void *context) {
    (void)sig; (void)info; (void)context;
    unsigned char local; stack_t current;
    CHECK((uintptr_t)&local >= (uintptr_t)alt && (uintptr_t)&local < (uintptr_t)alt+sizeof alt);
    CHECK(sigaltstack(NULL,&current)==0);
    if (autodisarm) { CHECK(current.ss_flags & SS_DISABLE); }
    else {
        CHECK(current.ss_flags & SS_ONSTACK);
        stack_t disabled={.ss_flags=SS_DISABLE};
        CHECK(sigaltstack(&disabled,NULL)==-1 && errno==EPERM);
    }
    signals++;
    if (!nested) { nested=1; CHECK(raise(SIGUSR1)==0); }
}
static void file_signal(int sig) { CHECK(sig==SIGXFSZ); fsize_signals++; }
static void *thread_check(void *unused) {
    (void)unused; stack_t current;
    CHECK(sigaltstack(NULL,&current)==0 && (current.ss_flags&SS_DISABLE));
    CHECK(syscall(SYS_membarrier,512,0)&16);
    struct rlimit r; CHECK(getrlimit(RLIMIT_FSIZE,&r)==0 && r.rlim_cur==7);
    r.rlim_cur=6; CHECK(setrlimit(RLIMIT_FSIZE,&r)==0);
    return NULL;
}
static void check_exec(void) {
    stack_t current; struct rlimit r;
    CHECK(sigaltstack(NULL,&current)==0 && (current.ss_flags&SS_DISABLE));
    CHECK(syscall(SYS_membarrier,512,0)==0);
    CHECK(getrlimit(RLIMIT_FSIZE,&r)==0 && r.rlim_cur==6);
}
int main(int argc,char **argv) {
    if (argc>1 && !strcmp(argv[1],"exec-child")) { check_exec(); return 0; }
    // musl sign-extends SA_RESETHAND's signed int into the kernel flags word.
    struct sigaction reset={.sa_handler=SIG_IGN,.sa_flags=SA_RESETHAND}, queried;
    CHECK(sigaction(SIGUSR2,&reset,NULL)==0);
    CHECK(sigaction(SIGUSR2,NULL,&queried)==0 && queried.sa_handler==SIG_IGN);
    CHECK((queried.sa_flags & SA_RESETHAND)==SA_RESETHAND);
    reset.sa_handler=SIG_DFL; reset.sa_flags=0;
    CHECK(sigaction(SIGUSR2,&reset,NULL)==0);
    stack_t current, installed={.ss_sp=alt,.ss_size=sizeof alt};
    CHECK(sigaltstack(NULL,&current)==0 && (current.ss_flags&SS_DISABLE));
    stack_t bad=installed; bad.ss_size=1;
    CHECK(sigaltstack(&bad,NULL)==-1 && errno==ENOMEM);
    bad=installed; bad.ss_flags=4;
    CHECK(syscall(SYS_sigaltstack,&bad,NULL)==-1 && errno==EINVAL);
#ifndef __GLIBC__
    bad.ss_flags=SS_ONSTACK;
    CHECK(sigaltstack(&bad,NULL)==-1 && errno==EINVAL);
#endif
    CHECK(sigaltstack(&installed,&current)==0 && (current.ss_flags&SS_DISABLE));
    struct sigaction sa={.sa_sigaction=handler,.sa_flags=SA_SIGINFO|SA_ONSTACK|SA_NODEFER};
    CHECK(sigaction(SIGUSR1,&sa,NULL)==0);
    CHECK(raise(SIGUSR1)==0 && signals==2);
    CHECK(sigaltstack(NULL,&current)==0 && current.ss_flags==0 && current.ss_sp==alt);
    // QEMU's generic altstack omits Linux SS_AUTODISARM. Exercise it on Trames
    // and the native Linux oracle separately, rather than accepting a false pass.
    if (argc>1 && !strcmp(argv[1],"autodisarm")) {
        installed.ss_flags=(int)0x80000000U;
        CHECK(sigaltstack(&installed,NULL)==0); autodisarm=1; nested=0;
        CHECK(raise(SIGUSR1)==0 && signals==4);
        CHECK(sigaltstack(NULL,&current)==0 && (unsigned)current.ss_flags==0x80000000U);
    }
    long supported=syscall(SYS_membarrier,0,0);
    CHECK(supported>=0 && (supported&(1|8|16))==(1|8|16));
    CHECK(syscall(SYS_membarrier,16,0)==0);
    CHECK(syscall(SYS_membarrier,8,0)==0);
    CHECK(syscall(SYS_membarrier,1,0)==0);
    CHECK(syscall(SYS_membarrier,0,1)==-1 && errno==EINVAL);
    CHECK(syscall(SYS_ioprio_get,1,0)>=0);
    CHECK(syscall(SYS_ioprio_get,1,getpid())>=0);
    CHECK(syscall(SYS_ioprio_get,1,0x7fffffff)==-1 && errno==ESRCH);
    struct rlimit original, limited; CHECK(getrlimit(RLIMIT_FSIZE,&original)==0);
    limited=original; limited.rlim_cur=7;
    struct rlimit previous;
    CHECK(prlimit(0,RLIMIT_FSIZE,&limited,&previous)==0 && previous.rlim_cur==original.rlim_cur);
    CHECK(sigaction(SIGXFSZ,&(struct sigaction){.sa_handler=file_signal},NULL)==0);
    int fd=open("runtime-limit",O_CREAT|O_TRUNC|O_RDWR,0600); CHECK(fd>=0);
    CHECK(write(fd,"0123456789",10)==7 && fsize_signals==0);
    CHECK(write(fd,"x",1)==-1 && errno==EFBIG && fsize_signals==1);
    CHECK(pwrite(fd,"x",1,7)==-1 && errno==EFBIG && fsize_signals==2);
    CHECK(ftruncate(fd,8)==-1 && errno==EFBIG && fsize_signals==3);
    CHECK(ftruncate(fd,3)==0);
    int pipefd[2]; CHECK(pipe(pipefd)==0 && write(pipefd[1],"abcdefghij",10)==10);
    char bytes[16]; CHECK(read(pipefd[0],bytes,10)==10); close(pipefd[0]); close(pipefd[1]);
    pid_t child=fork(); CHECK(child>=0);
    if (!child) {
        CHECK(sigaltstack(NULL,&current)==0 && current.ss_sp==alt);
        CHECK(syscall(SYS_membarrier,512,0)&16);
        limited.rlim_cur=5; CHECK(setrlimit(RLIMIT_FSIZE,&limited)==0); _exit(0);
    }
    int status; CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    CHECK(getrlimit(RLIMIT_FSIZE,&limited)==0 && limited.rlim_cur==7);
    pthread_t thread; CHECK(pthread_create(&thread,NULL,thread_check,NULL)==0);
    CHECK(pthread_join(thread,NULL)==0);
    CHECK(getrlimit(RLIMIT_FSIZE,&limited)==0 && limited.rlim_cur==6);
    if (!getenv("TRAMES_ORACLE_SKIP_EXEC")) {
        child=fork(); CHECK(child>=0);
        if (!child) { execl("/bin/runtime-compat","runtime-compat","exec-child",(char *)0); _exit(99); }
        CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    }
    CHECK(setrlimit(RLIMIT_FSIZE,&original)==0); close(fd); unlink("runtime-limit");
    CHECK(sigaltstack(&(stack_t){.ss_flags=SS_DISABLE},NULL)==0);
    puts("runtime-compat-ok"); return 0;
}
