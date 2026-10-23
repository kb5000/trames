// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/xattr.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s errno=%d\n",__LINE__,#x,errno); exit(1); } } while(0)
static struct sockaddr_un name;
static socklen_t length;
static int create(void) { int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0); CHECK(fd>=0); return fd; }
static void edge(int ep,int fd) { struct epoll_event e={.events=EPOLLIN|EPOLLET,.data.u64=42}; CHECK(epoll_ctl(ep,EPOLL_CTL_ADD,fd,&e)==0); }
static void ready(int ep,int count) { struct epoll_event e; CHECK(epoll_wait(ep,&e,1,0)==count); if(count) CHECK(e.data.u64==42 && e.events&EPOLLIN); }
static void endpoint(int abstract_) {
    memset(&name,0,sizeof(name)); name.sun_family=AF_UNIX;
    if(abstract_) { memcpy(name.sun_path,"\0trames\0unix",12); length=offsetof(struct sockaddr_un,sun_path)+12; }
    else { strcpy(name.sun_path,"/tmp/trames-unix.sock"); length=offsetof(struct sockaddr_un,sun_path)+strlen(name.sun_path)+1; unlink(name.sun_path); }
    int server=create(),client=create(),ep=epoll_create1(EPOLL_CLOEXEC); CHECK(ep>=0);
    CHECK(bind(server,(void*)&name,length)==0);
    CHECK(bind(client,(void*)&name,length)==-1 && errno==EADDRINUSE);
    CHECK(listen(server,0)==0); edge(ep,server); ready(ep,0);
    struct sockaddr_un got; socklen_t size=sizeof(got);
    CHECK(getsockname(server,(void*)&got,&size)==0 && size==length && !memcmp(&got,&name,size));
    if(!abstract_) { struct stat st; CHECK(lstat(name.sun_path,&st)==0 && S_ISSOCK(st.st_mode)); CHECK(open(name.sun_path,O_RDONLY)==-1 && errno==ENXIO); }
    CHECK(connect(client,(void*)&name,length)==0); ready(ep,1); ready(ep,0);
    int extra=create(); CHECK(connect(extra,(void*)&name,length)==-1 && errno==EAGAIN);
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); CHECK(guard!=MAP_FAILED);
    CHECK(mprotect(guard+page,page,PROT_NONE)==0);
    socklen_t peer_size=sizeof(struct sockaddr_un);
    // Only the actual unnamed peer address (two bytes) must be writable.
    int accepted=accept4(server,(void*)(guard+page-2),&peer_size,SOCK_NONBLOCK|SOCK_CLOEXEC); CHECK(accepted>=0 && peer_size==2);
    CHECK(munmap(guard,page*2)==0);
    CHECK(fcntl(accepted,F_GETFL)&O_NONBLOCK); CHECK(fcntl(accepted,F_GETFD)&FD_CLOEXEC);
    struct ucred cred; size=sizeof(cred); CHECK(getsockopt(accepted,SOL_SOCKET,SO_PEERCRED,&cred,&size)==0 && size==sizeof(cred) && cred.pid==getpid() && cred.uid==getuid() && cred.gid==getgid());
    CHECK(connect(extra,(void*)&name,length)==0); ready(ep,1); ready(ep,0);
    int second=accept(server,NULL,NULL); CHECK(second>=0); CHECK(!(fcntl(second,F_GETFL)&O_NONBLOCK));
    CHECK(accept(server,NULL,NULL)==-1 && errno==EAGAIN);
    CHECK(epoll_ctl(ep,EPOLL_CTL_DEL,server,NULL)==0); edge(ep,accepted); ready(ep,0);
    CHECK(write(client,"ab",2)==2); ready(ep,1); ready(ep,0);
    char b[10]; CHECK(read(accepted,b,1)==1 && b[0]=='a'); ready(ep,0);
    CHECK(write(client,"c",1)==1); ready(ep,1); ready(ep,0);
    CHECK(read(accepted,b,10)==2 && !memcmp(b,"bc",2)); ready(ep,0);
    CHECK(write(client,"d",1)==1); ready(ep,1); CHECK(read(accepted,b,10)==1); ready(ep,0);
    CHECK(shutdown(client,SHUT_WR)==0); ready(ep,1); CHECK(read(accepted,b,10)==0);
    close(server); close(second); close(extra); close(client); close(accepted); close(ep);
    int fresh=create();
    if(abstract_) CHECK(bind(fresh,(void*)&name,length)==0);
    else { CHECK(connect(fresh,(void*)&name,length)==-1 && errno==ECONNREFUSED); CHECK(bind(fresh,(void*)&name,length)==-1 && errno==EADDRINUSE); CHECK(unlink(name.sun_path)==0); CHECK(bind(fresh,(void*)&name,length)==0); CHECK(unlink(name.sun_path)==0); }
    close(fresh);
    puts(abstract_?"abstract Unix stream: binary names, lifecycle, backlog, credentials, ET: ok":"filesystem Unix stream: inode, stale path, backlog, credentials, ET: ok");
}
static void events(void) {
    int ep=epoll_create1(0),fd=eventfd(0,EFD_NONBLOCK); CHECK(ep>=0 && fd>=0); edge(ep,fd); ready(ep,0);
    unsigned long long x=1; CHECK(write(fd,&x,8)==8); ready(ep,1); ready(ep,0);
    CHECK(write(fd,&x,8)==8); ready(ep,1); ready(ep,0);
    CHECK(read(fd,&x,8)==8 && x==2); ready(ep,0);
    x=1; CHECK(write(fd,&x,8)==8); ready(ep,1); CHECK(read(fd,&x,8)==8); ready(ep,0);
    close(fd); close(ep); puts("eventfd ET: repeated writes and drain/refill: ok");
}
static void pipes(void) {
    int fds[2],ep=epoll_create1(0); CHECK(ep>=0 && pipe2(fds,O_NONBLOCK)==0); edge(ep,fds[0]); ready(ep,0);
    CHECK(write(fds[1],"a",1)==1); ready(ep,1); ready(ep,0);
    CHECK(write(fds[1],"b",1)==1); ready(ep,1); ready(ep,0);
    char b[10]; CHECK(read(fds[0],b,10)==2); ready(ep,0);
    CHECK(write(fds[1],"c",1)==1); ready(ep,1); CHECK(read(fds[0],b,10)==1); ready(ep,0);
    close(fds[1]); struct epoll_event e; CHECK(epoll_wait(ep,&e,1,0)==1 && e.events&EPOLLHUP);
    CHECK(epoll_wait(ep,&e,1,0)==0); close(fds[0]); close(ep);
    puts("pipe ET: repeated writes, drain/refill and peer close: ok");
}
static void symlink_times(void) {
    const char *path="/tmp/trames-utimens-link"; unlink(path);
    CHECK(symlink("/no-such-utimens-target",path)==0);
    const struct timespec times[2]={{.tv_sec=123,.tv_nsec=456},{.tv_sec=789,.tv_nsec=123}};
    CHECK(utimensat(AT_FDCWD,path,times,AT_SYMLINK_NOFOLLOW)==0);
    struct stat st; CHECK(lstat(path,&st)==0 && st.st_atim.tv_sec==123 && st.st_atim.tv_nsec==456 && st.st_mtim.tv_sec==789 && st.st_mtim.tv_nsec==123);
    CHECK(utimensat(AT_FDCWD,path,times,0)==-1 && errno==ENOENT); CHECK(unlink(path)==0);
    puts("archive timestamps: dangling symlink nofollow: ok");
}
static void attributes(void) {
    const char *path="/tmp/trames-attrs", *linkpath="/tmp/trames-attrs-link";
    unlink(path); unlink(linkpath);
    int fd=open(path,O_CREAT|O_RDWR|O_EXCL,0600); CHECK(fd>=0);
    char b[64];
    CHECK(getxattr(path,"user.test",b,sizeof(b))==-1 && errno==ENODATA);
    CHECK(setxattr(path,"user.test","a\0b",3,XATTR_CREATE)==0);
    CHECK(setxattr(path,"user.test","x",1,XATTR_CREATE)==-1 && errno==EEXIST);
    CHECK(getxattr(path,"user.test",NULL,0)==3);
    CHECK(fgetxattr(fd,"user.test",b,2)==-1 && errno==ERANGE);
    CHECK(fgetxattr(fd,"user.test",b,sizeof(b))==3 && !memcmp(b,"a\0b",3));
    CHECK(flistxattr(fd,NULL,0)==10);
    CHECK(listxattr(path,b,sizeof(b))==10 && !memcmp(b,"user.test\0",10));
    CHECK(symlink(path,linkpath)==0);
    CHECK(lgetxattr(linkpath,"user.test",b,sizeof(b))==-1 && errno==ENODATA);
    CHECK(getxattr(linkpath,"user.test",b,sizeof(b))==3);
    CHECK(lsetxattr(linkpath,"user.test","x",1,0)==-1 && errno==EPERM);
    CHECK(fsetxattr(fd,"user.test","new",3,XATTR_REPLACE)==0);
    CHECK(unlink(path)==0);
    CHECK(fgetxattr(fd,"user.test",b,sizeof(b))==3 && !memcmp(b,"new",3));
    CHECK(fremovexattr(fd,"user.test")==0);
    CHECK(fremovexattr(fd,"user.test")==-1 && errno==ENODATA);
    CHECK(flistxattr(fd,b,sizeof(b))==0);
    CHECK(fsetxattr(fd,"user.empty",NULL,0,0)==0);
    CHECK(fgetxattr(fd,"user.empty",NULL,0)==0);
    CHECK(close(fd)==0); CHECK(unlink(linkpath)==0);
    puts("inode xattrs: paths, fds, symlinks, binary values, flags and unlink: ok");
}
static void netlink_events(void) {
    int fd=socket(AF_NETLINK,SOCK_RAW|SOCK_NONBLOCK,0),ep=epoll_create1(0); CHECK(fd>=0 && ep>=0);
    unsigned char address[12]={16,0},request[32]={32,0,0,0,18,0,1,0,9,0,0,0};
    request[20]=1; // RTM_GETLINK, ifindex 1 (loopback in both kernels)
    CHECK(bind(fd,(void*)address,sizeof(address))==0); edge(ep,fd); ready(ep,0);
    for(int i=0;i<2;i++) {
        CHECK(sendto(fd,request,sizeof(request),0,(void*)address,sizeof(address))==(ssize_t)sizeof(request));
        struct epoll_event event;
        CHECK(epoll_wait(ep,&event,1,1000)==1 && event.events&EPOLLIN);
        ready(ep,0);
    }
    char b[4096]; CHECK(recv(fd,b,sizeof(b),0)>0); CHECK(recv(fd,b,sizeof(b),0)>0);
    CHECK(recv(fd,b,sizeof(b),0)==-1 && errno==EAGAIN); ready(ep,0);
    close(fd); close(ep); puts("netlink ET: repeated responses and drain: ok");
}
static void fifo_reopen(void) {
    const char *path="/tmp/trames-fifo-reopen"; unlink(path); CHECK(mkfifo(path,0600)==0);
    int anchor=open(path,O_PATH); CHECK(anchor>=0); char proc[80];
    snprintf(proc,sizeof(proc),"/proc/self/fd/%d",anchor);
    CHECK(unlink(path)==0);
    CHECK(open(proc,O_WRONLY|O_NONBLOCK)==-1 && errno==ENXIO);
    int reader=open(proc,O_RDONLY|O_NONBLOCK),writer=open(proc,O_WRONLY|O_NONBLOCK);
    CHECK(reader>=0 && writer>=0); CHECK(write(writer,"fifo",4)==4);
    char b[8]; CHECK(read(reader,b,8)==4 && !memcmp(b,"fifo",4));
    close(writer); CHECK(read(reader,b,8)==0); close(reader);
    CHECK(open(proc,O_WRONLY|O_NONBLOCK)==-1 && errno==ENXIO);
    close(anchor); puts("FIFO proc reopen: O_PATH, unlinked inode, independent endpoint lifetime: ok");
}
static void pipe_metadata(void) {
    int fds[2]; mode_t old=umask(0777); CHECK(pipe(fds)==0); umask(old);
    struct stat a,b; CHECK(fstat(fds[0],&a)==0 && S_ISFIFO(a.st_mode) && (a.st_mode&0777)==0600);
    CHECK(fchmod(fds[0],0640)==0); CHECK(fchown(fds[1],getuid(),getgid())==0);
    CHECK(fstat(fds[1],&b)==0 && b.st_ino==a.st_ino && (b.st_mode&0777)==0640 && b.st_uid==getuid());
    pid_t child=fork(); CHECK(child>=0);
    if(!child) { CHECK(fchmod(fds[1],0400)==0); _exit(0); }
    int status; CHECK(waitpid(child,&status,0)==child && status==0);
    CHECK(fstat(fds[0],&a)==0 && (a.st_mode&0777)==0400);
    CHECK(write(fds[1],"p",1)==1); char c; CHECK(read(fds[0],&c,1)==1 && c=='p');
    if(geteuid()==0) {
        CHECK(fchown(fds[0],1234,1234)==0); CHECK(fstat(fds[1],&b)==0 && b.st_uid==1234 && b.st_gid==1234);
        child=fork(); CHECK(child>=0); if(!child) { CHECK(setuid(1235)==0); CHECK(fchmod(fds[1],0600)==-1 && errno==EPERM); CHECK(fchown(fds[1],1235,1235)==-1 && errno==EPERM); _exit(0); }
        CHECK(waitpid(child,&status,0)==child && status==0);
    }
    close(fds[0]);close(fds[1]);puts("anonymous pipe inode: permissions, ownership, fork sharing and open access: ok");
}
static void fifo_links(void) {
    char dir[]="/tmp/trames-fifo-links-XXXXXX",path[128],alias[128];
    CHECK(mkdtemp(dir)!=NULL);
    CHECK(snprintf(path,sizeof(path),"%s/fifo",dir)>0);
    CHECK(snprintf(alias,sizeof(alias),"%s/alias",dir)>0);
    CHECK(mkfifo(path,0600)==0 && link(path,alias)==0);
    struct stat a,b; CHECK(stat(path,&a)==0 && stat(alias,&b)==0);
    CHECK(a.st_ino==b.st_ino && a.st_dev==b.st_dev && a.st_nlink==2);
    int reader=open(alias,O_RDONLY|O_NONBLOCK),writer=open(path,O_WRONLY|O_NONBLOCK);
    CHECK(reader>=0 && writer>=0);
    CHECK(unlink(path)==0 && mkfifo(path,0600)==0);
    CHECK(open(path,O_WRONLY|O_NONBLOCK)==-1 && errno==ENXIO);
    int fresh=open(path,O_RDWR|O_NONBLOCK); CHECK(fresh>=0);
    CHECK(fstat(fresh,&b)==0 && a.st_ino!=b.st_ino);
    CHECK(write(writer,"old",3)==3 && write(fresh,"new",3)==3);
    char bytes[8]; CHECK(read(reader,bytes,sizeof(bytes))==3 && !memcmp(bytes,"old",3));
    CHECK(read(fresh,bytes,sizeof(bytes))==3 && !memcmp(bytes,"new",3));
    CHECK(unlink(alias)==0 && fstat(reader,&a)==0 && a.st_nlink==0);
    CHECK(write(writer,"live",4)==4 && read(reader,bytes,sizeof(bytes))==4 && !memcmp(bytes,"live",4));
    close(writer); CHECK(read(reader,bytes,sizeof(bytes))==0); close(reader);
    close(fresh); CHECK(unlink(path)==0 && rmdir(dir)==0);
    puts("FIFO hardlinks: shared channel, replaced path isolation and unlinked open lifetime: ok");
}
int main(void) { CHECK(mkdir("/tmp",0777)==0 || errno==EEXIST); endpoint(0); endpoint(1); events(); pipes(); symlink_times(); attributes(); netlink_events(); fifo_reopen(); pipe_metadata(); fifo_links(); return 0; }
