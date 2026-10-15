// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s errno=%d\n",__LINE__,#x,errno); exit(1); } } while (0)
static sigset_t mask;
static int fd;
static volatile sig_atomic_t handled;
static void handler(int sig) { (void)sig; handled++; }
static void block(int sig) { sigemptyset(&mask); sigaddset(&mask,sig); CHECK(pthread_sigmask(SIG_BLOCK,&mask,NULL)==0); }
static void setup(void) { block(SIGUSR1); fd=signalfd(-1,&mask,SFD_NONBLOCK|SFD_CLOEXEC); CHECK(fd>=0); }
static int ready(int n) { struct pollfd p={.fd=n,.events=POLLIN|POLLOUT}; CHECK(poll(&p,1,0)>=0); return p.revents; }
static struct signalfd_siginfo take(int n) { struct signalfd_siginfo s; CHECK(read(n,&s,sizeof s)==sizeof s); return s; }
static void empty(int n) { struct signalfd_siginfo s; CHECK(read(n,&s,sizeof s)==-1 && errno==EAGAIN); }
static void wait_ok(pid_t p) { int status; CHECK(waitpid(p,&status,0)==p && WIFEXITED(status) && WEXITSTATUS(status)==0); }
static void parameters(void) {
    setup(); uint64_t bits=~0ULL;
    CHECK(syscall(SYS_signalfd4,-1,&bits,7,0)==-1 && errno==EINVAL);
    CHECK(syscall(SYS_signalfd4,-1,0,8,0)==-1 && errno==EFAULT);
    CHECK(syscall(SYS_signalfd4,-1,&bits,8,1)==-1 && errno==EINVAL);
    CHECK(syscall(SYS_signalfd4,-2,&bits,8,0)==-1 && errno==EBADF);
    CHECK(syscall(SYS_signalfd4,1,&bits,8,0)==-1 && errno==EINVAL);
    CHECK(fcntl(fd,F_GETFD)==FD_CLOEXEC && (fcntl(fd,F_GETFL)&O_NONBLOCK));
    CHECK(read(fd,&bits,0)==-1 && errno==EINVAL);
    CHECK(read(fd,&bits,7)==-1 && errno==EINVAL);
    CHECK(readv(fd,NULL,0)==0);
    CHECK(write(fd,&bits,8)==-1 && errno==EINVAL);
    CHECK(lseek(fd,99,SEEK_SET)==0);
    CHECK(ready(fd)==0); empty(fd);
    sigaddset(&mask,SIGKILL); sigaddset(&mask,SIGSTOP);
    CHECK(signalfd(fd,&mask,0)==fd);
    char path[80],out[1024]; snprintf(path,sizeof path,"/proc/self/fd/%d",fd);
    ssize_t n=readlink(path,out,sizeof out-1); CHECK(n>0); out[n]=0; CHECK(!strcmp(out,"anon_inode:[signalfd]"));
    snprintf(path,sizeof path,"/proc/self/fdinfo/%d",fd);
    int f=open(path,O_RDONLY); CHECK(f>=0); n=read(f,out,sizeof out-1); CHECK(n>0); out[n]=0; close(f);
    CHECK(strstr(out,"sigmask:\t0000000000000200\n"));
}
static void coalesce(void) {
    setup(); CHECK(kill(getpid(),SIGUSR1)==0 && kill(getpid(),SIGUSR1)==0);
    CHECK(ready(fd)==POLLIN);
    sigset_t pending; CHECK(sigpending(&pending)==0 && sigismember(&pending,SIGUSR1)==1);
    struct signalfd_siginfo s=take(fd); CHECK(s.ssi_signo==SIGUSR1 && s.ssi_code==SI_USER && s.ssi_pid==(unsigned)getpid() && s.ssi_uid==getuid());
    CHECK(!s.ssi_status && !s.ssi_ptr && !s.ssi_addr); empty(fd);
    CHECK(sigpending(&pending)==0 && sigismember(&pending,SIGUSR1)==0);
}
static void realtime(void) {
    int rt=SIGRTMIN+2; block(rt); fd=signalfd(-1,&mask,SFD_NONBLOCK); CHECK(fd>=0);
    CHECK(kill(getpid(),rt)==0 && kill(getpid(),rt)==0 && kill(getpid(),rt)==0);
    struct signalfd_siginfo s[4]; CHECK(read(fd,s,sizeof s)==3*128);
    for(int i=0;i<3;i++) CHECK(s[i].ssi_signo==(unsigned)rt && s[i].ssi_pid==(unsigned)getpid());
    empty(fd);
}
static void realtime_fifo(void) {
    int rt=SIGRTMIN+2; block(rt); sigaddset(&mask,rt+1); CHECK(pthread_sigmask(SIG_BLOCK,&mask,NULL)==0);
    fd=signalfd(-1,&mask,SFD_NONBLOCK); CHECK(fd>=0); pid_t parent=getpid();
    pid_t a=fork(); CHECK(a>=0); if(!a) { CHECK(kill(parent,rt+1)==0 && kill(parent,rt)==0 && kill(parent,rt)==0); _exit(0); } wait_ok(a);
    pid_t b=fork(); CHECK(b>=0); if(!b) { CHECK(kill(parent,rt)==0); _exit(0); } wait_ok(b);
    struct signalfd_siginfo s[4]; CHECK(read(fd,s,sizeof s)==sizeof s);
    CHECK(s[0].ssi_pid==(unsigned)a && s[1].ssi_pid==(unsigned)a && s[2].ssi_pid==(unsigned)b && s[3].ssi_pid==(unsigned)a);
    CHECK(s[0].ssi_signo==(unsigned)rt && s[1].ssi_signo==(unsigned)rt && s[2].ssi_signo==(unsigned)rt && s[3].ssi_signo==(unsigned)(rt+1)); empty(fd);
}
static void private_first(void) {
    setup(); sigaddset(&mask,SIGUSR2); CHECK(pthread_sigmask(SIG_BLOCK,&mask,NULL)==0); CHECK(signalfd(fd,&mask,0)==fd);
    CHECK(kill(getpid(),SIGUSR1)==0);
    CHECK(syscall(SYS_tgkill,getpid(),syscall(SYS_gettid),SIGUSR2)==0);
    struct signalfd_siginfo s[2]; CHECK(read(fd,s,sizeof s)==sizeof s);
    CHECK(s[0].ssi_signo==SIGUSR2 && s[0].ssi_code==SI_TKILL && s[1].ssi_signo==SIGUSR1);
    empty(fd);
}
static void signal_priority(void) {
    setup(); sigaddset(&mask,SIGSEGV); CHECK(pthread_sigmask(SIG_BLOCK,&mask,NULL)==0); CHECK(signalfd(fd,&mask,0)==fd);
    CHECK(kill(getpid(),SIGUSR1)==0 && kill(getpid(),SIGSEGV)==0);
    struct signalfd_siginfo s[2]; CHECK(read(fd,s,sizeof s)==sizeof s);
    CHECK(s[0].ssi_signo==SIGSEGV && s[0].ssi_code==SI_USER && s[1].ssi_signo==SIGUSR1); empty(fd);
}
static void vector_read(void) {
    setup(); CHECK(kill(getpid(),SIGUSR1)==0);
    unsigned char data[128]; struct iovec parts[]={{data,31},{data+31,0},{data+31,97}};
    CHECK(readv(fd,parts,3)==128); struct signalfd_siginfo s; memcpy(&s,data,sizeof s); CHECK(s.ssi_signo==SIGUSR1); empty(fd);
    CHECK(kill(getpid(),SIGUSR1)==0); unsigned char odd[129]; CHECK(read(fd,odd,sizeof odd)==128); empty(fd);
}
static void copy_fault(void) {
    setup(); long page=sysconf(_SC_PAGESIZE); void *bad=mmap(NULL,(size_t)page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); CHECK(bad!=MAP_FAILED);
    CHECK(kill(getpid(),SIGUSR1)==0); CHECK(read(fd,bad,128)==-1 && errno==EFAULT); empty(fd);
    int rt=SIGRTMIN+2; block(rt); CHECK(signalfd(fd,&mask,0)==fd);
    CHECK(kill(getpid(),rt)==0 && kill(getpid(),rt)==0);
    struct signalfd_siginfo s; struct iovec parts[]={{&s,128},{bad,128}};
    CHECK(readv(fd,parts,2)==128 && s.ssi_signo==(unsigned)rt); empty(fd); CHECK(munmap(bad,(size_t)page)==0);
}
static void mask_dup(void) {
    setup(); int dupfd=dup(fd); CHECK(dupfd>=0 && fcntl(dupfd,F_GETFD)==0);
    CHECK(kill(getpid(),SIGUSR1)==0); block(SIGUSR2);
    CHECK(signalfd(dupfd,&mask,0)==dupfd && (fcntl(fd,F_GETFL)&O_NONBLOCK) && fcntl(fd,F_GETFD)==FD_CLOEXEC);
    empty(fd); CHECK(kill(getpid(),SIGUSR2)==0); CHECK(take(fd).ssi_signo==SIGUSR2);
    sigemptyset(&mask); sigaddset(&mask,SIGUSR1); CHECK(signalfd(fd,&mask,SFD_CLOEXEC)==fd); CHECK(take(dupfd).ssi_signo==SIGUSR1);
    CHECK(fcntl(fd,F_SETFL,0)==0 && !(fcntl(dupfd,F_GETFL)&O_NONBLOCK));
}
static void epoll_checks(void) {
    setup(); int ep=epoll_create1(EPOLL_CLOEXEC); CHECK(ep>=0);
    struct epoll_event event={.events=EPOLLIN|EPOLLONESHOT,.data.u64=123},out;
    CHECK(epoll_ctl(ep,EPOLL_CTL_ADD,fd,&event)==0); CHECK(epoll_wait(ep,&out,1,0)==0);
    CHECK(kill(getpid(),SIGUSR1)==0); CHECK(ready(ep)==POLLIN);
    CHECK(epoll_wait(ep,&out,1,0)==1 && out.data.u64==123 && out.events==EPOLLIN);
    CHECK(epoll_wait(ep,&out,1,0)==0); CHECK(epoll_ctl(ep,EPOLL_CTL_MOD,fd,&event)==0);
    CHECK(epoll_wait(ep,&out,1,0)==1); CHECK(take(fd).ssi_signo==SIGUSR1);
    event.events=EPOLLIN; CHECK(epoll_ctl(ep,EPOLL_CTL_MOD,fd,&event)==0);
    int alias=dup(fd); CHECK(alias>=0); close(fd); CHECK(kill(getpid(),SIGUSR1)==0);
    CHECK(epoll_wait(ep,&out,1,0)==1); CHECK(take(alias).ssi_signo==SIGUSR1); CHECK(epoll_wait(ep,&out,1,0)==0);
    CHECK(kill(getpid(),SIGUSR1)==0); close(alias); CHECK(epoll_wait(ep,&out,1,0)==0);
}
static void select_check(void) {
    setup(); fd_set r; FD_ZERO(&r); FD_SET(fd,&r); struct timespec zero={0};
    CHECK(pselect(fd+1,&r,NULL,NULL,&zero,NULL)==0);
    CHECK(kill(getpid(),SIGUSR1)==0); FD_SET(fd,&r);
    CHECK(pselect(fd+1,&r,NULL,NULL,&zero,NULL)==1 && FD_ISSET(fd,&r)); CHECK(take(fd).ssi_signo==SIGUSR1);
}
static void inherited(void) {
    setup(); CHECK(kill(getpid(),SIGUSR1)==0);
    pid_t child=fork(); CHECK(child>=0);
    if(!child) { empty(fd); CHECK(kill(getpid(),SIGUSR1)==0); CHECK(take(fd).ssi_pid==(unsigned)getpid()); _exit(0); }
    wait_ok(child); CHECK(take(fd).ssi_pid==(unsigned)getpid()); empty(fd);
}
static void exec_inherit(void) {
    setup(); int keep=dup(fd); CHECK(keep>=0); CHECK(kill(getpid(),SIGUSR1)==0);
    char a[20],b[20]; snprintf(a,sizeof a,"%d",keep); snprintf(b,sizeof b,"%d",fd);
    execl("/proc/self/exe","signalfd","exec-child",a,b,(char*)0); CHECK(0);
}
static void exec_child(char **argv) {
    int keep=atoi(argv[2]),closed=atoi(argv[3]);
    CHECK(fcntl(closed,F_GETFD)==-1 && errno==EBADF);
    CHECK(take(keep).ssi_pid==(unsigned)getpid()); empty(keep);
}
static void child_exit(void) {
    block(SIGCHLD); fd=signalfd(-1,&mask,0); CHECK(fd>=0);
    pid_t child=fork(); CHECK(child>=0); if(!child) _exit(7);
    struct signalfd_siginfo s=take(fd); CHECK(s.ssi_signo==SIGCHLD && s.ssi_pid==(unsigned)child && s.ssi_uid==getuid() && s.ssi_code==CLD_EXITED && s.ssi_status==7);
    int status; CHECK(waitpid(child,&status,0)==child && WEXITSTATUS(status)==7);
}
static int pipes[2];
static void *worker(void *unused) {
    (void)unused; char c; CHECK(read(pipes[0],&c,1)==1);
    empty(fd); // Main-thread-only signal is not visible here.
    CHECK(kill(getpid(),SIGUSR2)==0); struct signalfd_siginfo s=take(fd); CHECK(s.ssi_signo==SIGUSR2 && s.ssi_code==SI_USER);
    CHECK(syscall(SYS_tgkill,getpid(),syscall(SYS_gettid),SIGUSR2)==0); s=take(fd); CHECK(s.ssi_signo==SIGUSR2 && s.ssi_code==SI_TKILL);
    return NULL;
}
static void thread_scope(void) {
    setup(); sigaddset(&mask,SIGUSR2); CHECK(pthread_sigmask(SIG_BLOCK,&mask,NULL)==0); CHECK(signalfd(fd,&mask,0)==fd); CHECK(pipe(pipes)==0);
    pthread_t thread; CHECK(pthread_create(&thread,NULL,worker,NULL)==0);
    CHECK(syscall(SYS_tgkill,getpid(),syscall(SYS_gettid),SIGUSR1)==0); CHECK(write(pipes[1],"x",1)==1);
    CHECK(pthread_join(thread,NULL)==0); CHECK(take(fd).ssi_signo==SIGUSR1); empty(fd);
}
static void *delayed(void *mode) {
    usleep(20000);
    if(mode) { CHECK(kill(getpid(),SIGUSR2)==0); usleep(20000); }
    CHECK(kill(getpid(),SIGUSR1)==0); return NULL;
}
static void blocking(void) {
    setup(); CHECK(fcntl(fd,F_SETFL,0)==0); pthread_t t; CHECK(pthread_create(&t,NULL,delayed,NULL)==0);
    CHECK(take(fd).ssi_signo==SIGUSR1); CHECK(pthread_join(t,NULL)==0);
}
static void interruption(int restart) {
    setup(); CHECK(fcntl(fd,F_SETFL,0)==0);
    CHECK(sigaction(SIGUSR2,&(struct sigaction){.sa_handler=handler,.sa_flags=restart?SA_RESTART:0},NULL)==0);
    // The helper also blocks SIGUSR2 so the handler must interrupt this reader.
    block(SIGUSR2); pthread_t t; CHECK(pthread_create(&t,NULL,delayed,(void*)1)==0); CHECK(pthread_sigmask(SIG_UNBLOCK,&mask,NULL)==0);
    struct signalfd_siginfo s;
    if(restart) { CHECK(read(fd,&s,sizeof s)==sizeof s && s.ssi_signo==SIGUSR1); }
    else { CHECK(read(fd,&s,sizeof s)==-1 && errno==EINTR); CHECK(take(fd).ssi_signo==SIGUSR1); }
    CHECK(handled==1 && pthread_join(t,NULL)==0);
}
static void blocked_epoll(void) {
    setup(); int ep=epoll_create1(0); CHECK(ep>=0); struct epoll_event event={.events=EPOLLIN,.data.u64=99},out;
    CHECK(epoll_ctl(ep,EPOLL_CTL_ADD,fd,&event)==0); pthread_t t; CHECK(pthread_create(&t,NULL,delayed,NULL)==0);
    CHECK(epoll_pwait(ep,&out,1,2000,&mask)==1 && out.events==EPOLLIN && out.data.u64==99);
    CHECK(take(fd).ssi_signo==SIGUSR1 && pthread_join(t,NULL)==0);
    sigset_t restored; CHECK(pthread_sigmask(SIG_SETMASK,NULL,&restored)==0 && sigismember(&restored,SIGUSR1));
}
static void blocked_ppoll(void) {
    setup(); pthread_t t; CHECK(pthread_create(&t,NULL,delayed,NULL)==0);
    struct pollfd p={.fd=fd,.events=POLLIN}; struct timespec timeout={2,0};
    CHECK(ppoll(&p,1,&timeout,&mask)==1 && p.revents==POLLIN); CHECK(take(fd).ssi_signo==SIGUSR1 && pthread_join(t,NULL)==0);
}
static void ignored_blocked(void) {
    setup(); CHECK(sigaction(SIGUSR1,&(struct sigaction){.sa_handler=SIG_IGN},NULL)==0);
    CHECK(kill(getpid(),SIGUSR1)==0 && take(fd).ssi_signo==SIGUSR1);
}
static void interrupted(void) { interruption(0); }
static void restarted(void) { interruption(1); }
static void disposition_flush(void) {
    setup(); CHECK(kill(getpid(),SIGUSR1)==0);
    CHECK(syscall(SYS_tgkill,getpid(),syscall(SYS_gettid),SIGUSR1)==0);
    CHECK(sigaction(SIGUSR1,&(struct sigaction){.sa_handler=SIG_IGN},NULL)==0); empty(fd);
    block(SIGCHLD); CHECK(signalfd(fd,&mask,0)==fd); CHECK(kill(getpid(),SIGCHLD)==0); CHECK(ready(fd)==POLLIN);
    CHECK(sigaction(SIGCHLD,&(struct sigaction){.sa_handler=SIG_DFL},NULL)==0); empty(fd);
}
static void ordinary_handler(void) {
    setup(); CHECK(sigaction(SIGUSR1,&(struct sigaction){.sa_handler=handler},NULL)==0);
    CHECK(kill(getpid(),SIGUSR1)==0); CHECK(pthread_sigmask(SIG_UNBLOCK,&mask,NULL)==0); CHECK(handled==1); empty(fd);
}
static void wait_competes(void) {
    setup(); CHECK(kill(getpid(),SIGUSR1)==0); siginfo_t info; struct timespec zero={0};
    CHECK(sigtimedwait(&mask,&info,&zero)==SIGUSR1 && info.si_pid==getpid()); empty(fd);
    CHECK(kill(getpid(),SIGUSR1)==0); CHECK(close(fd)==0); CHECK(sigtimedwait(&mask,&info,&zero)==SIGUSR1);
}
static void timer_signal(void) {
    int sig=SIGRTMIN+2; block(sig); fd=signalfd(-1,&mask,0); CHECK(fd>=0);
    struct sigevent event={.sigev_notify=SIGEV_SIGNAL,.sigev_signo=sig,.sigev_value.sival_ptr=(void*)(uintptr_t)0x1234567887654321ULL}; timer_t timer;
    CHECK(timer_create(CLOCK_MONOTONIC,&event,&timer)==0); struct itimerspec arm={.it_value={0,1000000},.it_interval={0,1000000}};
    CHECK(timer_settime(timer,0,&arm,NULL)==0); usleep(20000); struct signalfd_siginfo s=take(fd);
    CHECK(s.ssi_signo==(unsigned)sig && s.ssi_code==SI_TIMER && s.ssi_ptr==0x1234567887654321ULL && s.ssi_int==(int)0x87654321U && s.ssi_overrun>=1);
    CHECK(timer_delete(timer)==0);
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc>1) { CHECK(argc==4 && !strcmp(argv[1],"exec-child")); exec_child(argv); return 0; }
    struct {const char *name; void (*run)(void);} tests[]={
        {"parameters, flags and proc metadata",parameters},{"standard coalescing and pending bits",coalesce},
        {"realtime queue multiplicity",realtime},{"realtime priority and sender FIFO",realtime_fifo},{"private before shared delivery",private_first},{"Linux synchronous-class priority",signal_priority},
        {"vectored and whole-record reads",vector_read},{"copy faults and partial success",copy_fault},
        {"dup-shared masks and independent descriptor flags",mask_dup},{"epoll readiness, oneshot and last-close lifetime",epoll_checks},
        {"pselect readiness",select_check},{"fork reader-local pending signals",inherited},{"exec pending and CLOEXEC",exec_inherit},
        {"SIGCHLD metadata and wait coexistence",child_exit},{"thread-private and process-shared queues",thread_scope},
        {"blocking read wakeup",blocking},{"interrupted read",interrupted},{"SA_RESTART read",restarted},
        {"blocked epoll wakeup",blocked_epoll},{"blocked ppoll wakeup",blocked_ppoll},{"blocked ignored signal",ignored_blocked},
        {"disposition changes discard pending signals",disposition_flush},{"ordinary handler consumption",ordinary_handler},{"sigtimedwait and close coexistence",wait_competes},{"POSIX timer metadata and overrun",timer_signal},
    };
    for(size_t i=0;i<sizeof tests/sizeof tests[0];i++) { pid_t p=fork(); CHECK(p>=0); if(!p) { tests[i].run(); _exit(0); } wait_ok(p); printf("%s: ok\n",tests[i].name); }
    return 0;
}
