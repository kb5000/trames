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
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#define CG "/sys/fs/cgroup"
#define DENIED(expr, code) do { errno=0; assert((expr)==-1 && errno==(code)); } while (0)
static void read_file(const char *path, char *out, size_t size) {
    int f=open(path,O_RDONLY); assert(f>=0); ssize_t n=read(f,out,size-1); assert(n>=0); out[n]=0; close(f);
}
static void expect(const char *path, const char *value) { char buf[2048]; read_file(path,buf,sizeof buf); assert(!strcmp(buf,value)); }
static void write_file(const char *path, const char *text) {
    int f=open(path,O_WRONLY|O_TRUNC); assert(f>=0); assert(write(f,text,strlen(text))==(ssize_t)strlen(text)); close(f);
}
static void move(const char *path, pid_t pid) { char text[32]; snprintf(text,sizeof text,"%d\n",pid); write_file(path,text); }
static void waited(pid_t pid) { int s; assert(waitpid(pid,&s,0)==pid && WIFEXITED(s) && WEXITSTATUS(s)==0); }
static int count(const char *path) { char text[2048]; read_file(path,text,sizeof text); int n=0; for(char *p=text;*p;p++)n+=*p=='\n'; return n; }
static _Atomic int ready, release;
static void *member(void *unused) {
    (void)unused;
    atomic_store(&ready,1); while(!atomic_load(&release)) sched_yield();
    expect("/proc/thread-self/cgroup","0::/lab/leaf\n"); return NULL;
}
static void *exec_member(void *unused) { (void)unused; execl("/cgroups","cgroups","exec",NULL); assert(0); return NULL; }
static void *forever(void *unused) { (void)unused; atomic_store(&ready,1); for (;;) pause(); return NULL; }
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"fresh")) {
        DENIED(access(CG "/lab", F_OK),ENOENT); expect("/proc/self/cgroup","0::/\n");
        puts("cgroup session state is not persisted to the root disk: ok"); return 0;
    }
    if(argc==2 && !strcmp(argv[1],"exec")) {
        expect("/proc/self/cgroup","0::/lab/leaf\n");
        assert(count(CG "/lab/leaf/cgroup.procs")==1 && count(CG "/lab/leaf/cgroup.threads")==1); return 0;
    }
    struct statfs fs; assert(statfs(CG,&fs)==0 && (unsigned long)fs.f_type==0x63677270UL);
    expect(CG "/cgroup.controllers","\n"); expect("/proc/self/cgroup","0::/\n");
    char text[8192]; read_file("/proc/self/mountinfo",text,sizeof text); assert(strstr(text," /sys/fs/cgroup rw,") && strstr(text," - cgroup2 cgroup2 rw"));
    assert(mkdir(CG "/lab",0755)==0 && mkdir(CG "/lab/leaf",0755)==0);
    expect(CG "/lab/cgroup.type","domain\n"); expect(CG "/lab/cgroup.events","populated 0\nfrozen 0\n");
    expect(CG "/lab/cgroup.stat","nr_descendants 1\nnr_dying_descendants 0\n");
    DENIED(rmdir(CG "/lab"),ENOTEMPTY);
    int f=open(CG "/lab/cgroup.subtree_control",O_WRONLY); assert(f>=0);
    DENIED(write(f,"+memory",7),EOPNOTSUPP); assert(write(f,"\n",1)==1); close(f);
    DENIED(open(CG "/lab/injected",O_CREAT|O_WRONLY,0600),EPERM);
    f=open(CG "/lab/cgroup.procs",O_RDWR); assert(f>=0);
    DENIED(write(f,"-1",2),EINVAL); DENIED(write(f,"1 2",3),EINVAL); DENIED(write(f,"999999",6),ESRCH);
    errno=0; assert(mmap(NULL,4096,PROT_READ,MAP_PRIVATE,f,0)==MAP_FAILED && errno==ENODEV); close(f);
    move(CG "/lab/cgroup.procs",0); expect("/proc/self/cgroup","0::/lab\n");
    expect(CG "/lab/cgroup.events","populated 1\nfrozen 0\n");
    DENIED(rmdir(CG "/lab"),ENOTEMPTY);
    pthread_t thread; assert(pthread_create(&thread,NULL,member,NULL)==0);
    while(!atomic_load(&ready)) sched_yield();
    assert(count(CG "/lab/cgroup.procs")==1 && count(CG "/lab/cgroup.threads")==2);
    move(CG "/lab/leaf/cgroup.procs",getpid());
    expect("/proc/self/cgroup","0::/lab/leaf\n");
    assert(count(CG "/lab/leaf/cgroup.threads")==2);
    atomic_store(&release,1); assert(pthread_join(thread,NULL)==0);
    assert(count(CG "/lab/leaf/cgroup.threads")==1);
    DENIED(rmdir(CG "/lab/leaf"),EBUSY);
    int go[2], done[2]; assert(pipe(go)==0 && pipe(done)==0);
    pid_t child=fork(); assert(child>=0);
    if(!child) { char b; assert(read(go[0],&b,1)==1); expect("/proc/self/cgroup","0::/lab\n"); assert(write(done[1],"x",1)==1); _exit(0); }
    assert(count(CG "/lab/leaf/cgroup.procs")==2);
    move(CG "/lab/cgroup.procs",child); assert(write(go[1],"x",1)==1); char b; assert(read(done[0],&b,1)==1); waited(child);
    close(go[0]);close(go[1]);close(done[0]);close(done[1]);
    assert(count(CG "/lab/cgroup.procs")==0);
    move(CG "/cgroup.procs",0);
    expect(CG "/lab/cgroup.events","populated 0\nfrozen 0\n");
    child=fork(); assert(child>=0);
    if(!child) {
        move(CG "/lab/leaf/cgroup.procs",0);
        assert(pthread_create(&thread,NULL,exec_member,NULL)==0); for(;;) pause();
    }
    waited(child); assert(count(CG "/lab/leaf/cgroup.threads")==0);
    child=fork(); assert(child>=0);
    if(!child) { move(CG "/lab/leaf/cgroup.procs",0); atomic_store(&ready,0); assert(pthread_create(&thread,NULL,forever,NULL)==0); while(!atomic_load(&ready))sched_yield(); _exit(0); }
    waited(child); assert(count(CG "/lab/leaf/cgroup.threads")==0);
    child=fork(); assert(child>=0);
    if(!child) {
        assert(unshare(CLONE_NEWPID)==0); pid_t init=fork(); assert(init>=0);
        if(!init) {
            assert(getpid()==1); move(CG "/lab/leaf/cgroup.procs",1);
            expect(CG "/lab/leaf/cgroup.procs","1\n");
            expect(CG "/cgroup.procs","");
            int live[2]; assert(pipe(live)==0); pid_t member=fork(); assert(member>=0);
            if(!member) { assert(write(live[1],"x",1)==1); for(;;) pause(); }
            assert(read(live[0],&b,1)==1);
            // Init exit must remove the still-running descendant's membership.
            _exit(0);
        }
        waited(init); _exit(0);
    }
    waited(child); assert(count(CG "/lab/leaf/cgroup.procs")==0);
    assert(mkdir("/cg",0755)==0 && mount("none","/cg","cgroup2",0,NULL)==0);
    assert(statfs("/cg",&fs)==0 && (unsigned long)fs.f_type==0x63677270UL);
    struct stat a,bstat; assert(stat(CG "/lab",&a)==0 && stat("/cg/lab",&bstat)==0 && a.st_ino==bstat.st_ino);
    child=fork(); assert(child>=0);
    if(!child) {
        assert(unshare(CLONE_NEWNS)==0); assert(mount(NULL,"/cg",NULL,MS_REMOUNT|MS_RDONLY,NULL)==0);
        DENIED(open("/cg/lab/cgroup.procs",O_WRONLY),EROFS); _exit(0);
    }
    waited(child); f=open("/cg/lab/cgroup.procs",O_WRONLY); assert(f>=0); close(f);
    int stale=open("/cg/lab/leaf/cgroup.procs",O_RDWR); assert(stale>=0);
    assert(rmdir(CG "/lab/leaf")==0);
    expect(CG "/lab/cgroup.stat","nr_descendants 0\nnr_dying_descendants 1\n");
    assert(mkdir(CG "/lab/leaf",0755)==0);
    DENIED(write(stale,"0",1),ENODEV); DENIED(read(stale,text,sizeof text),ENODEV); close(stale);
    expect(CG "/lab/cgroup.stat","nr_descendants 1\nnr_dying_descendants 0\n");
    assert(mkdir("/cg-stale",0755)==0 && mount(CG "/lab/leaf","/cg-stale",NULL,MS_BIND,NULL)==0);
    assert(rmdir(CG "/lab/leaf")==0 && mkdir(CG "/lab/leaf",0755)==0);
    DENIED(open("/cg-stale/cgroup.procs",O_WRONLY),ENODEV);
    // The detached binding remains a retained object until the namespace exits.
    assert(umount("/cg")==0);
    child=fork(); assert(child>=0);
    if(!child) { assert(setuid(1000)==0); DENIED(mkdir(CG "/denied",0755),EPERM); DENIED(open(CG "/lab/cgroup.procs",O_WRONLY),EACCES); _exit(0); }
    waited(child);
    puts("cgroup v2: hierarchy, membership, threads/exec/exit, PID views, mounts, stale FDs and denied controls: ok");
    return 0;
}
