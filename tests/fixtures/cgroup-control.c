// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define CG "/sys/fs/cgroup/control"
#define DENIED(expr, code) do { errno=0; assert((expr)==-1 && errno==(code)); } while(0)
static struct shared { _Atomic unsigned ticks, signals, ready; } *s;
static void handler(int sig) { (void)sig; atomic_fetch_add(&s->signals,1); }
static void put(const char *path,const char *text) {
    int fd=open(path,O_WRONLY); assert(fd>=0);
    assert(write(fd,text,strlen(text))==(ssize_t)strlen(text)); close(fd);
}
static void expect(const char *path,const char *text) {
    char buf[128]; int fd=open(path,O_RDONLY); assert(fd>=0);
    ssize_t n=read(fd,buf,sizeof buf-1); assert(n>=0); buf[n]=0; close(fd);
    assert(!strcmp(buf,text));
}
static void move(const char *path,pid_t pid) { char buf[32]; snprintf(buf,sizeof buf,"%d",pid); put(path,buf); }
static void changed(void) { unsigned before=atomic_load(&s->ticks); for(int i=0;i<1000 && atomic_load(&s->ticks)==before;i++)usleep(1000); assert(atomic_load(&s->ticks)!=before); }
static void quiet(void) { unsigned before=atomic_load(&s->ticks); usleep(30000); assert(atomic_load(&s->ticks)==before); }
static void waitready(unsigned n) { for(int i=0;i<1000 && atomic_load(&s->ready)<n;i++)usleep(1000); assert(atomic_load(&s->ready)==n); }
static void *spin(void *unused) { (void)unused; atomic_fetch_add(&s->ready,1); for(;;)atomic_fetch_add(&s->ticks,1); return NULL; }
int main(void) {
    s=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0); assert(s!=MAP_FAILED);
    assert(mkdir(CG,0755)==0 && mkdir(CG "/leaf",0755)==0);
    DENIED(access("/sys/fs/cgroup/cgroup.freeze",F_OK),ENOENT);
    DENIED(access("/sys/fs/cgroup/cgroup.kill",F_OK),ENOENT);
    int fd=open(CG "/cgroup.freeze",O_WRONLY); assert(fd>=0);
    DENIED(write(fd,"2",1),EINVAL); assert(write(fd,"",0)==0);
    close(fd);
    expect(CG "/cgroup.freeze","0\n"); expect(CG "/cgroup.events","populated 0\nfrozen 0\n");
    pid_t child=fork(); assert(child>=0);
    if(!child) { move(CG "/leaf/cgroup.procs",0); assert(signal(SIGUSR1,handler)!=SIG_ERR); spin(NULL); _exit(1); }
    waitready(1); changed();
    put(CG "/cgroup.freeze","1\n"); quiet();
    expect(CG "/cgroup.events","populated 1\nfrozen 1\n");
    expect(CG "/leaf/cgroup.events","populated 1\nfrozen 1\n");
    expect(CG "/leaf/cgroup.freeze","0\n");
    assert(kill(child,SIGUSR1)==0); usleep(20000); assert(!atomic_load(&s->signals)); quiet();
    put(CG "/leaf/cgroup.freeze","0"); quiet(); // ancestor still frozen
    move("/sys/fs/cgroup/cgroup.procs",child); changed();
    for(int i=0;i<1000 && !atomic_load(&s->signals);i++)usleep(1000);
    assert(atomic_load(&s->signals)==1);
    move(CG "/leaf/cgroup.procs",child); quiet();
    put(CG "/leaf/cgroup.freeze","1"); put(CG "/cgroup.freeze","0"); quiet();
    expect(CG "/cgroup.events","populated 1\nfrozen 0\n");
    put(CG "/leaf/cgroup.freeze","0"); changed();
    assert(kill(child,SIGSTOP)==0); int status;
    assert(waitpid(child,&status,WUNTRACED)==child && WIFSTOPPED(status));
    put(CG "/cgroup.freeze","1"); put(CG "/cgroup.freeze","0"); quiet();
    assert(kill(child,SIGCONT)==0); changed();
    put(CG "/cgroup.freeze","1"); assert(kill(child,SIGKILL)==0);
    assert(waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
    expect(CG "/cgroup.events","populated 0\nfrozen 1\n");
    // Moving a task into a frozen subtree prevents even its next fork/exec.
    atomic_store(&s->ready,0); int go[2]; assert(pipe(go)==0);
    child=fork(); assert(child>=0);
    if(!child) { char b; assert(read(go[0],&b,1)==1); spin(NULL); _exit(1); }
    move(CG "/leaf/cgroup.procs",child); assert(write(go[1],"x",1)==1);
    usleep(20000); assert(!atomic_load(&s->ready));
    put(CG "/cgroup.freeze","0"); waitready(1); changed();
    put(CG "/cgroup.freeze","1"); quiet();
    fd=open(CG "/cgroup.kill",O_WRONLY); assert(fd>=0); DENIED(write(fd,"0",1),EINVAL); close(fd);
    put(CG "/cgroup.kill","1");
    assert(waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
    close(go[0]); close(go[1]);
    expect(CG "/cgroup.events","populated 0\nfrozen 1\n");
    // Whole thread groups and descendant processes, without killing siblings.
    put(CG "/cgroup.freeze","0"); atomic_store(&s->ready,0);
    child=fork(); assert(child>=0);
    if(!child) {
        move(CG "/leaf/cgroup.procs",0);
        pthread_t thread; assert(pthread_create(&thread,NULL,spin,NULL)==0);
        spin(NULL); _exit(1);
    }
    waitready(2); put(CG "/cgroup.freeze","1"); quiet(); put(CG "/cgroup.kill","1");
    assert(waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
    expect(CG "/leaf/cgroup.procs",""); expect(CG "/leaf/cgroup.threads","");
    assert(rmdir(CG "/leaf")==0 && rmdir(CG)==0);
    puts("cgroup control: hierarchical freeze, migration, pending signals, stop/continue, blocked IO and subtree kill: ok");
    return 0;
}
