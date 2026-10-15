// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s errno=%d\n",__LINE__,#x,errno); _exit(1); } } while(0)
static int channel[2];
static pid_t child_id;
static sigset_t signals;
static void await_death(pid_t parent) {
    CHECK(prctl(PR_SET_PDEATHSIG,SIGUSR1)==0);
    int sig=-1; CHECK(prctl(PR_GET_PDEATHSIG,&sig)==0 && sig==SIGUSR1);
    CHECK(write(channel[1],"r",1)==1);
    siginfo_t info; struct timespec timeout={.tv_sec=5};
    CHECK(sigtimedwait(&signals,&info,&timeout)==SIGUSR1 && info.si_pid==parent && info.si_uid==getuid());
    _exit(0);
}
static void *parent_thread(void *unused) {
    (void)unused; pid_t parent=getpid(); child_id=fork(); CHECK(child_id>=0);
    if(!child_id) await_death(parent);
    char c; CHECK(read(channel[0],&c,1)==1); return NULL;
}
static void waited(pid_t pid) { int status; CHECK(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0); }
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"exec")) { int sig; CHECK(prctl(PR_GET_PDEATHSIG,&sig)==0 && sig==SIGUSR1); return 0; }
    sigemptyset(&signals); sigaddset(&signals,SIGUSR1); CHECK(sigprocmask(SIG_BLOCK,&signals,NULL)==0);
    CHECK(prctl(PR_SET_PDEATHSIG,65)==-1 && errno==EINVAL);
    CHECK(prctl(PR_SET_PDEATHSIG,SIGUSR1)==0);
    pid_t child=fork(); CHECK(child>=0);
    if(!child) { int sig=-1; CHECK(prctl(PR_GET_PDEATHSIG,&sig)==0 && sig==0); CHECK(prctl(PR_SET_PDEATHSIG,SIGUSR1)==0); execl("/proc/self/exe",argv[0],"exec",NULL); _exit(2); }
    waited(child); CHECK(prctl(PR_SET_PDEATHSIG,0)==0);
    puts("parent death signal: validation, fork reset and exec preservation: ok");
    CHECK(pipe(channel)==0); CHECK(prctl(PR_SET_CHILD_SUBREAPER,1)==0);
    pid_t parent=fork(); CHECK(parent>=0);
    if(!parent) { pid_t id=getpid(); pid_t c=fork(); CHECK(c>=0); if(!c) await_death(id); char b; CHECK(read(channel[0],&b,1)==1); _exit(0); }
    waited(parent); int status; CHECK(waitpid(-1,&status,0)>0 && WIFEXITED(status) && WEXITSTATUS(status)==0);
    puts("parent death signal: process exit delivers sender metadata: ok");
    pthread_t thread; CHECK(pthread_create(&thread,NULL,parent_thread,NULL)==0); CHECK(pthread_join(thread,NULL)==0); waited(child_id);
    puts("parent death signal: creating thread exit while process stays alive: ok");
    close(channel[0]); close(channel[1]);
    if(geteuid()==0) { child=fork(); CHECK(child>=0); if(!child) { CHECK(prctl(PR_SET_PDEATHSIG,SIGUSR1)==0); CHECK(setresuid(1000,1000,1000)==0); int sig=-1; CHECK(prctl(PR_GET_PDEATHSIG,&sig)==0 && sig==0); _exit(0); } waited(child); }
    return 0;
}
