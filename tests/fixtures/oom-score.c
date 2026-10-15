// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s errno=%d\n",__LINE__,#x,errno); _exit(1); } } while(0)
static int read_score(int fd) { char b[32]={0}; CHECK(pread(fd,b,sizeof(b)-1,0)>0); return atoi(b); }
static void write_score(int fd,const char *s) { CHECK(write(fd,s,strlen(s))==(ssize_t)strlen(s)); }
static void waited(pid_t p) { int s; CHECK(waitpid(p,&s,0)==p && WIFEXITED(s) && WEXITSTATUS(s)==0); }
static void *thread(void *unused) { (void)unused; char path[128]; snprintf(path,sizeof(path),"/proc/%d/task/%ld/oom_score_adj",getpid(),syscall(SYS_gettid)); int fd=open(path,O_RDWR); CHECK(fd>=0 && read_score(fd)==400); write_score(fd,"500"); close(fd); return NULL; }
int main(int argc,char **argv) {
    int fd=open("/proc/self/oom_score_adj",O_RDWR); CHECK(fd>=0);
    if(argc==2) { CHECK(!strcmp(argv[1],"exec") && read_score(fd)==600); return 0; }
    // Drop SYS_RESOURCE so host container capabilities and guest root agree.
    struct { unsigned version; int pid; } hdr={0x20080522,0};
    struct { unsigned effective,permitted,inheritable; } caps[2];
    CHECK(syscall(SYS_capget,&hdr,caps)==0); caps[0].effective &= ~(1U<<24); CHECK(syscall(SYS_capset,&hdr,caps)==0);
    write_score(fd,"400\n"); CHECK(read_score(fd)==400 && lseek(fd,0,SEEK_CUR)==0);
    CHECK(write(fd,"1001",4)==-1 && errno==EINVAL); CHECK(write(fd,"-1001",5)==-1 && errno==EINVAL);
    CHECK(write(fd,"garbage",7)==-1 && errno==EINVAL); CHECK(write(fd,"",0)==-1 && errno==EINVAL);
    CHECK(write(fd,"-1",2)==-1 && errno==EACCES);
    CHECK(pwrite(fd,"0x190",5,17)==5 && read_score(fd)==400);
    char b[8]; CHECK(pread(fd,b,2,1)==2 && !memcmp(b,"00",2)); CHECK(lseek(fd,0,SEEK_END)==0);
    pthread_t t; CHECK(pthread_create(&t,NULL,thread,NULL)==0); CHECK(pthread_join(t,NULL)==0); CHECK(read_score(fd)==500);
    write_score(fd,"400");
    pid_t p=fork(); CHECK(p>=0); if(!p) { int own=open("/proc/self/oom_score_adj",O_RDWR|O_TRUNC); CHECK(own>=0 && read_score(own)==400); write_score(own,"600"); execl("/proc/self/exe",argv[0],"exec",NULL); _exit(2); } waited(p); CHECK(read_score(fd)==400);
    int ch[2]; CHECK(pipe(ch)==0); p=fork(); CHECK(p>=0); if(!p) { close(ch[1]); char c; CHECK(read(ch[0],&c,1)==1); _exit(0); } close(ch[0]);
    char path[128]; snprintf(path,sizeof(path),"/proc/%d/oom_score_adj",p); int child=open(path,O_RDWR); CHECK(child>=0); CHECK(write(ch[1],"x",1)==1); close(ch[1]); waited(p);
    CHECK(pread(child,b,sizeof(b),0)==-1 && errno==ESRCH); CHECK(write(child,"400",3)==-1 && errno==ESRCH); close(child); close(fd);
    puts("oom_score_adj: range, permissions, offsets, threads, fork, exec and dead task: ok"); return 0;
}
