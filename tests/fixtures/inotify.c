// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <sys/inotify.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s errno=%d\n",__LINE__,#x,errno); exit(1); } } while (0)
static char buffer[65536] __attribute__((aligned(8)));
static ssize_t size;
static void collect(int fd) {
    int available=-1; CHECK(ioctl(fd,FIONREAD,&available)==0); CHECK(available>0);
    size=read(fd,buffer,sizeof(buffer)); CHECK(size>0 && size==available);
}
static unsigned event(int wd,unsigned mask,const char *name) {
    for (ssize_t at=0;at<size;) {
        struct inotify_event *e=(void *)(buffer+at);
        CHECK(at+16+e->len<=size && e->len%16==0);
        if(e->wd==wd && (e->mask&mask)==mask && (!name || (e->len && !strcmp(e->name,name)))) return e->cookie ? e->cookie : 1;
        at+=16+e->len;
    }
    return 0;
}
int main(void) {
    CHECK(mkdir("/tmp",0777)==0 || errno==EEXIST);
    char dir[96]; snprintf(dir,sizeof(dir),"/tmp/inotify-%d",getpid());
    CHECK(mkdir(dir,0700)==0); CHECK(chdir(dir)==0);
    int in=inotify_init1(IN_NONBLOCK|IN_CLOEXEC); CHECK(in>=0);
    CHECK(read(in,buffer,sizeof(buffer))==-1 && errno==EAGAIN);
    int wd=inotify_add_watch(in,".",IN_ALL_EVENTS); CHECK(wd>0);
    CHECK(inotify_add_watch(in,".",IN_CREATE|IN_MASK_CREATE)==-1 && errno==EEXIST);
    int fd=open("a",O_CREAT|O_RDWR,0600); CHECK(fd>=0);
    int file=inotify_add_watch(in,"a",IN_ALL_EVENTS); CHECK(file>0);
    collect(in); CHECK(event(wd,IN_CREATE,"a") && event(wd,IN_OPEN,"a"));
    CHECK(write(fd,"hello",5)==5); CHECK(pwrite(fd,"!",1,4)==1);
    collect(in); CHECK(event(wd,IN_MODIFY,"a") && event(file,IN_MODIFY,NULL));
    CHECK(fchmod(fd,0640)==0); CHECK(ftruncate(fd,3)==0);
    collect(in); CHECK(event(file,IN_ATTRIB,NULL) && event(file,IN_MODIFY,NULL));
    CHECK(rename("a","b")==0); collect(in);
    unsigned from=event(wd,IN_MOVED_FROM,"a"),to=event(wd,IN_MOVED_TO,"b");
    CHECK(from && from==to && event(file,IN_MOVE_SELF,NULL));
    CHECK(pread(fd,buffer,1,0)==1); collect(in); CHECK(event(wd,IN_ACCESS,"b"));
    int duplicate=dup(fd); CHECK(duplicate>=0); CHECK(close(fd)==0);
    CHECK(read(in,buffer,sizeof(buffer))==-1 && errno==EAGAIN);
    CHECK(close(duplicate)==0); collect(in); CHECK(event(file,IN_CLOSE_WRITE,NULL) && event(wd,IN_CLOSE_WRITE,"b"));
    CHECK(inotify_add_watch(in,".",IN_CREATE)==wd);
    CHECK(inotify_add_watch(in,".",IN_DELETE|IN_MASK_ADD)==wd);
    CHECK(unlink("b")==0); collect(in); CHECK(event(wd,IN_DELETE,"b") && event(file,IN_DELETE_SELF,NULL) && event(file,IN_IGNORED,NULL));
    CHECK(mkdir("child",0700)==0); collect(in); CHECK(event(wd,IN_CREATE|IN_ISDIR,"child"));
    CHECK(inotify_rm_watch(in,wd)==0); collect(in); CHECK(event(wd,IN_IGNORED,NULL));
    int once=inotify_add_watch(in,".",IN_CREATE|IN_ONESHOT); CHECK(once>0);
    fd=open("one",O_CREAT|O_WRONLY,0600); CHECK(fd>=0); CHECK(close(fd)==0);
    errno=0; CHECK(read(in,buffer,15)==-1 && errno==EINVAL);
    collect(in); CHECK(event(once,IN_CREATE,"one") && event(once,IN_IGNORED,NULL));
    CHECK(inotify_rm_watch(in,once)==-1 && errno==EINVAL);
    int ep=epoll_create1(0); CHECK(ep>=0);
    struct epoll_event ev={.events=EPOLLIN|EPOLLET,.data.u64=42},out;
    CHECK(epoll_ctl(ep,EPOLL_CTL_ADD,in,&ev)==0);
    CHECK(inotify_add_watch(in,".",IN_CREATE)>0);
    pid_t child=fork(); CHECK(child>=0);
    if(!child) { usleep(20000); int f=open("fork",O_CREAT|O_WRONLY,0600); _exit(f<0 || close(f)<0); }
    CHECK(epoll_wait(ep,&out,1,2000)==1 && out.data.u64==42);
    collect(in); int status; CHECK(waitpid(child,&status,0)==child && status==0);
    CHECK(epoll_wait(ep,&out,1,0)==0);
    fd=open("next",O_CREAT|O_WRONLY,0600); CHECK(fd>=0); CHECK(close(fd)==0);
    CHECK(epoll_wait(ep,&out,1,2000)==1); collect(in);
    CHECK(close(ep)==0 && close(in)==0);
    int blocking=inotify_init1(IN_CLOEXEC); CHECK(blocking>=0);
    int blockwd=inotify_add_watch(blocking,".",IN_CREATE); CHECK(blockwd>0);
    child=fork(); CHECK(child>=0);
    if(!child) { usleep(20000); int f=open("blocking",O_CREAT|O_WRONLY,0600); _exit(f<0 || close(f)<0); }
    size=read(blocking,buffer,sizeof(buffer)); CHECK(size>0 && event(blockwd,IN_CREATE,"blocking"));
    CHECK(waitpid(child,&status,0)==child && status==0);
    CHECK(close(blocking)==0 && unlink("blocking")==0);
    int anonymous=memfd_create("inotify",0); CHECK(anonymous>=0);
    char magic[64]; snprintf(magic,sizeof(magic),"/proc/self/fd/%d",anonymous);
    in=inotify_init1(IN_NONBLOCK); CHECK(in>=0);
    int anonwd=inotify_add_watch(in,magic,IN_ALL_EVENTS); CHECK(anonwd>0);
    CHECK(inotify_add_watch(in,magic,IN_ACCESS|IN_ONLYDIR)==-1 && errno==ENOTDIR);
    CHECK(write(anonymous,"x",1)==1);
    CHECK(read(in,buffer,sizeof(buffer))==-1 && errno==EAGAIN); // Original memfd: FMODE_NONOTIFY.
    int reopened=open(magic,O_RDWR); CHECK(reopened>=0); collect(in); CHECK(event(anonwd,IN_OPEN,NULL));
    CHECK(write(reopened,"y",1)==1); collect(in); CHECK(event(anonwd,IN_MODIFY,NULL));
    CHECK(read(reopened,buffer,1)==0); CHECK(read(in,buffer,sizeof(buffer))==-1 && errno==EAGAIN);
    CHECK(close(reopened)==0); collect(in); CHECK(event(anonwd,IN_CLOSE_WRITE,NULL));
    CHECK(close(anonymous)==0); collect(in);
    CHECK(event(anonwd,IN_DELETE_SELF,NULL) && event(anonwd,IN_IGNORED,NULL));
    CHECK(close(in)==0);
    CHECK(unlink("one")==0 && unlink("fork")==0 && unlink("next")==0 && rmdir("child")==0);
    CHECK(chdir("/")==0 && rmdir(dir)==0);
    puts("inotify-ok"); return 0;
}
