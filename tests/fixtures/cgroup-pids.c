// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#define ROOT "/sys/fs/cgroup"
#define CG ROOT "/quota"
#define DENIED(expr, code) do { errno=0; assert((expr)==-1 && errno==(code)); } while(0)
static void put(const char *path,const char *value) { int fd=open(path,O_WRONLY); assert(fd>=0); assert(write(fd,value,strlen(value))==(ssize_t)strlen(value)); close(fd); }
static void expect(const char *path,const char *value) { char buf[256]; int fd=open(path,O_RDONLY); assert(fd>=0); ssize_t n=read(fd,buf,sizeof(buf)-1); assert(n>=0); buf[n]=0; close(fd); assert(!strcmp(buf,value)); }
static void waited(pid_t pid) { int status; assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && !WEXITSTATUS(status)); }
static _Atomic int ready, release;
static void *member(void *unused) { (void)unused; atomic_store(&ready,1); while(!atomic_load(&release))sched_yield(); return NULL; }
int main(void) {
    expect(ROOT "/cgroup.controllers","pids\n");
    assert(mkdir(CG,0755)==0 && mkdir(CG "/leaf",0755)==0);
    DENIED(access(CG "/pids.max",F_OK),ENOENT);
    int ctrl=open(CG "/cgroup.subtree_control",O_WRONLY); assert(ctrl>=0);
    DENIED(write(ctrl,"+pids",5),ENOENT);
    put(ROOT "/cgroup.subtree_control","+pids");
    expect(CG "/pids.max","max\n"); expect(CG "/pids.current","0\n");
    put(CG "/cgroup.subtree_control","+pids");
    expect(CG "/leaf/pids.max","max\n"); expect(CG "/cgroup.subtree_control","pids\n");
    DENIED(write(ctrl,"-pids +memory",13),EOPNOTSUPP);
    expect(CG "/cgroup.subtree_control","pids\n");
    int rootctrl=open(ROOT "/cgroup.subtree_control",O_WRONLY); assert(rootctrl>=0);
    DENIED(write(rootctrl,"-pids",5),EBUSY);
    put(CG "/pids.max","1"); put(CG "/leaf/pids.max","8");
    pid_t child=fork(); assert(child>=0);
    if(!child) {
        put(CG "/leaf/cgroup.procs","0");
        expect(CG "/pids.current","1\n"); expect(CG "/leaf/pids.current","1\n");
        DENIED(fork(),EAGAIN);
        expect(CG "/pids.events","max 1\n"); expect(CG "/pids.events.local","max 1\n");
        expect(CG "/leaf/pids.events","max 0\n");
        put(CG "/pids.max","2"); pthread_t thread;
        assert(pthread_create(&thread,NULL,member,NULL)==0); while(!atomic_load(&ready))sched_yield();
        expect(CG "/pids.current","2\n"); DENIED(fork(),EAGAIN);
        atomic_store(&release,1); assert(pthread_join(thread,NULL)==0);
        expect(CG "/pids.current","1\n");
        pid_t next=fork(); assert(next>=0); if(!next)_exit(0); waited(next);
        expect(CG "/pids.current","1\n"); expect(CG "/pids.peak","2\n");
        put(CG "/pids.max","0"); expect(CG "/pids.current","1\n"); DENIED(fork(),EAGAIN);
        // Reorganization may exceed the limit. Existing tasks are not killed.
        put(ROOT "/cgroup.procs","0"); put(CG "/leaf/cgroup.procs","0"); DENIED(fork(),EAGAIN);
        put(CG "/pids.max","max"); put(CG "/leaf/pids.max","1"); DENIED(fork(),EAGAIN);
        expect(CG "/leaf/pids.events.local","max 1\n"); expect(CG "/pids.events","max 5\n");
        put(CG "/leaf/pids.max","2"); next=fork(); assert(next>=0); if(!next)_exit(0); waited(next);
        _exit(0);
    }
    waited(child); expect(CG "/pids.current","0\n");
    int stale=open(CG "/leaf/pids.max",O_RDWR); assert(stale>=0);
    put(CG "/cgroup.subtree_control","-pids");
    DENIED(write(stale,"1",1),ENODEV); DENIED(access(CG "/leaf/pids.max",F_OK),ENOENT);
    put(CG "/cgroup.subtree_control","+pids");
    DENIED(write(stale,"1",1),ENODEV); close(stale);
    expect(CG "/leaf/pids.max","max\n"); expect(CG "/leaf/pids.events","max 0\n");
    int limit=open(CG "/leaf/pids.max",O_WRONLY); assert(limit>=0);
    DENIED(write(limit,"-1",2),EINVAL); DENIED(write(limit,"4194305",7),EINVAL); DENIED(write(limit,"1 2",3),EINVAL);
    assert(write(limit,"0x10",4)==4); expect(CG "/leaf/pids.max","16\n"); close(limit);
    // Two CPUs race the last available slot; neither can overcommit it.
    struct race { _Atomic int ready, go, done, success; } *r=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    assert(r!=MAP_FAILED); put(CG "/pids.max","3");
    for(int i=0;i<2;i++) {
        child=fork(); assert(child>=0);
        if(!child) {
            put(CG "/leaf/cgroup.procs","0"); atomic_fetch_add(&r->ready,1);
            while(!atomic_load(&r->go))sched_yield();
            pid_t next=fork();
            if(!next) { for(;;)pause(); }
            if(next>0)atomic_fetch_add(&r->success,1); else assert(errno==EAGAIN);
            atomic_fetch_add(&r->done,1); for(;;)pause();
        }
    }
    while(atomic_load(&r->ready)!=2)usleep(1000);
    atomic_store(&r->go,1);
    while(atomic_load(&r->done)!=2)usleep(1000);
    assert(atomic_load(&r->success)==1); expect(CG "/pids.current","3\n");
    put(CG "/cgroup.kill","1");
    int status, reaped=0; while(waitpid(-1,&status,0)>0) { assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL); reaped++; }
    assert(reaped==3 && errno==ECHILD); expect(CG "/pids.current","0\n"); assert(munmap(r,4096)==0);
    close(ctrl); close(rootctrl); assert(rmdir(CG "/leaf")==0 && rmdir(CG)==0);
    puts("pids controller: activation, ancestor limits, fork/threads/exit, migration, events, peak and stale controls: ok");
}
