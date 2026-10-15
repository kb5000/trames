// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
struct header { uint32_t version; int pid; };
struct word { uint32_t e,p,i; };
static long dump(int n) { return prctl(PR_SET_DUMPABLE,(unsigned long)n,0UL,0UL,0UL); }
static void waited(pid_t p) { int s; assert(waitpid(p,&s,0)==p && WIFEXITED(s) && WEXITSTATUS(s)==0); }
static void check_read(const char *path,int allowed) {
    int f=open(path,O_RDONLY); if(allowed) { assert(f>=0); close(f); } else { assert(f==-1 && errno==EACCES); }
}
static void *shared(void *unused) { (void)unused; assert(prctl(PR_GET_DUMPABLE)==0 && dump(1)==0); return NULL; }
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"exec")) { assert(prctl(PR_GET_DUMPABLE)==1); return 0; }
    if(argc==2 && !strcmp(argv[1],"exec-gain")) { assert(prctl(PR_GET_DUMPABLE)==0); return 0; }
    if(argc==2 && !strcmp(argv[1],"ids")) {
        pid_t p=fork(); assert(p>=0);
        if(!p) {
            struct header h={0x20080522,0}; struct word d[2]; assert(syscall(SYS_capget,&h,d)==0);
            d[0].e &= ~(1U<<19); d[0].p &= ~(1U<<19); assert(syscall(SYS_capset,&h,d)==0);
            // Root exec restores bounding capabilities, so this is privileged.
            assert(dump(1)==0); execl(argv[0],argv[0],"exec-gain",NULL); _exit(2);
        }
        waited(p);
        assert(dump(1)==0 && setresuid(1000,1000,1000)==0 && prctl(PR_GET_DUMPABLE)==0);
        assert(dump(1)==0 && setresuid(1000,1000,1000)==0 && prctl(PR_GET_DUMPABLE)==1);
        puts("dumpable credential transitions: ok"); return 0;
    }
    // Drop the ptrace override in both parent and child, including on root CI.
    struct header h={0x20080522,0}; struct word d[2]; assert(syscall(SYS_capget,&h,d)==0);
    d[0].e &= ~(1U<<19); d[0].p &= ~(1U<<19); assert(syscall(SYS_capset,&h,d)==0);
    assert(dump(1)==0 && prctl(PR_GET_DUMPABLE)==1);
    errno=0; assert(dump(2)==-1 && errno==EINVAL && prctl(PR_GET_DUMPABLE)==1);
    assert(dump(0)==0); check_read("/proc/self/maps",1);
    pid_t p=fork(); assert(p>=0); if(!p) { assert(prctl(PR_GET_DUMPABLE)==0 && dump(1)==0); _exit(0); }
    waited(p); assert(prctl(PR_GET_DUMPABLE)==0);
    pthread_t t; assert(pthread_create(&t,NULL,shared,NULL)==0 && pthread_join(t,NULL)==0);
    assert(prctl(PR_GET_DUMPABLE)==1);
    int go[2],ack[2]; assert(pipe(go)==0 && pipe(ack)==0); p=fork(); assert(p>=0);
    if(!p) {
        char c; assert(dump(0)==0 && write(ack[1],"x",1)==1);
        assert(read(go[0],&c,1)==1 && dump(1)==0 && write(ack[1],"x",1)==1);
        assert(read(go[0],&c,1)==1); _exit(0);
    }
    char c,path[128],link[512]; assert(read(ack[0],&c,1)==1);
    struct stat st;
    snprintf(path,sizeof path,"/proc/%d/maps",p); assert(stat(path,&st)==0);
    int metadata=open(path,O_PATH); assert(metadata>=0); close(metadata);
    snprintf(path,sizeof path,"/proc/%d/exe",p); assert(lstat(path,&st)==0);
    for(size_t i=0;i<4;i++) {
        const char *name[]={"maps","environ","auxv","ns/pid"};
        snprintf(path,sizeof path,"/proc/%d/%s",p,name[i]); check_read(path,0);
    }
    snprintf(path,sizeof path,"/proc/%d/exe",p); errno=0; assert(readlink(path,link,sizeof link)==-1 && errno==EACCES);
    snprintf(path,sizeof path,"/proc/%d/status",p); check_read(path,1);
    assert(write(go[1],"x",1)==1 && read(ack[0],&c,1)==1);
    snprintf(path,sizeof path,"/proc/%d/maps",p); check_read(path,1);
    snprintf(path,sizeof path,"/proc/%d/ns/pid",p); check_read(path,1);
    assert(write(go[1],"x",1)==1); waited(p);
    close(go[0]);close(go[1]);close(ack[0]);close(ack[1]);
    p=fork(); assert(p>=0); if(!p) {
        // Equalize privilege across the unprivileged host and root guest oracle.
        assert(prctl(PR_SET_NO_NEW_PRIVS,1UL,0UL,0UL,0UL)==0 && dump(0)==0);
        execl(argv[0],argv[0],"exec",NULL); _exit(2);
    } waited(p);
    puts("dumpable: validation, fork/VM sharing, exec reset and sensitive proc denial: ok"); return 0;
}
