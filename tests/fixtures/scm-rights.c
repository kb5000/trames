// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
static ssize_t sendfds(int sock,const int *fds,size_t n,const char *text) {
    union { struct cmsghdr align; char bytes[CMSG_SPACE(16*sizeof(int))]; } c={0};
    assert(n<=16); struct iovec v={(void*)text,strlen(text)};
    struct msghdr m={.msg_iov=&v,.msg_iovlen=1,.msg_control=c.bytes,.msg_controllen=CMSG_SPACE(n*sizeof(int))};
    struct cmsghdr *h=CMSG_FIRSTHDR(&m); h->cmsg_level=SOL_SOCKET; h->cmsg_type=SCM_RIGHTS; h->cmsg_len=CMSG_LEN(n*sizeof(int));
    memcpy(CMSG_DATA(h),fds,n*sizeof(int)); return sendmsg(sock,&m,0);
}
static ssize_t recvfds(int sock,int *fds,size_t *n,size_t cap,char *text,size_t len,int flags,int *outflags) {
    union { struct cmsghdr align; char bytes[CMSG_SPACE(16*sizeof(int))]; } c={0};
    struct iovec v={text,len}; struct msghdr m={.msg_iov=&v,.msg_iovlen=1,.msg_control=c.bytes,.msg_controllen=cap};
    ssize_t result=recvmsg(sock,&m,flags); if(result<0)return result;
    *n=0; *outflags=m.msg_flags;
    for(struct cmsghdr *h=CMSG_FIRSTHDR(&m);h;h=CMSG_NXTHDR(&m,h)) {
        assert(h->cmsg_level==SOL_SOCKET && h->cmsg_type==SCM_RIGHTS);
        *n=(h->cmsg_len-CMSG_LEN(0))/sizeof(int); assert(*n<=16); memcpy(fds,CMSG_DATA(h),*n*sizeof(int));
    }
    return result;
}
static void one_kind(int kind) {
    int pair[2]; assert(socketpair(AF_UNIX,kind,0,pair)==0);
    int file=memfd_create("rights",MFD_CLOEXEC); assert(file>=0); assert(write(file,"abcdef",6)==6); assert(lseek(file,0,SEEK_SET)==0);
    assert(sendfds(pair[0],&file,1,"abc")==3);
    int fds[16],flags; size_t n; char text[32];
    assert(recvfds(pair[1],fds,&n,sizeof(struct cmsghdr)+sizeof(int),text,1,MSG_PEEK|MSG_CMSG_CLOEXEC,&flags)==1);
    assert(n==1 && fcntl(fds[0],F_GETFD)==FD_CLOEXEC); assert(read(fds[0],text,1)==1 && *text=='a'); close(fds[0]);
    assert(recvfds(pair[1],fds,&n,CMSG_SPACE(sizeof(int)),text,sizeof text,0,&flags)==3);
    assert(n==1 && fcntl(fds[0],F_GETFD)==0); assert(read(fds[0],text,1)==1 && *text=='b');
    close(file); assert(read(fds[0],text,1)==1 && *text=='c'); close(fds[0]);
    int pipefd[2]; assert(pipe(pipefd)==0);
    assert(sendfds(pair[0],&pipefd[1],1,"x")==1); close(pipefd[1]);
    assert(recvfds(pair[1],fds,&n,0,text,1,0,&flags)==1 && n==0 && (flags&MSG_CTRUNC));
    assert(read(pipefd[0],text,1)==0); close(pipefd[0]);
    file=memfd_create("many",0); assert(file>=0); int many[3]={file,file,file};
    assert(sendfds(pair[0],many,3,"x")==1);
    assert(recvfds(pair[1],fds,&n,CMSG_LEN(sizeof(int)),text,1,0,&flags)==1 && n==1 && (flags&MSG_CTRUNC)); close(fds[0]);
    int bad=-1; errno=0; assert(sendfds(pair[0],&bad,1,"bad")==-1 && errno==EBADF);
    struct pollfd pollfd={pair[1],POLLIN,0}; assert(poll(&pollfd,1,0)==0);
    // A queued reference survives sender close and crosses fork without sharing FD numbers.
    int sync[2]; assert(pipe(sync)==0); pid_t child=fork(); assert(child>=0);
    if(!child) { close(pair[0]); close(file); close(sync[1]); assert(read(sync[0],text,1)==1); assert(recvfds(pair[1],fds,&n,sizeof(struct cmsghdr)+8,text,sizeof text,MSG_CMSG_CLOEXEC,&flags)==1 && n==1); assert(write(fds[0],"guest",5)==5); close(fds[0]); _exit(0); }
    close(pair[1]); close(sync[0]); assert(sendfds(pair[0],&file,1,"x")==1); assert(write(sync[1],"x",1)==1); close(sync[1]);
    int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
    assert(lseek(file,0,SEEK_SET)==0 && read(file,text,5)==5 && !memcmp(text,"guest",5));
    close(file); close(pair[0]);
}
int main(void) {
    one_kind(SOCK_STREAM); one_kind(SOCK_DGRAM); one_kind(SOCK_SEQPACKET);
    int p[2]; assert(socketpair(AF_UNIX,SOCK_STREAM,0,p)==0);
    int fd=memfd_create("barrier",0); assert(fd>=0);
    assert(write(p[0],"before",6)==6); assert(sendfds(p[0],&fd,1,"rights")==6); assert(write(p[0],"after",5)==5);
    char buf[32]; int fds[16],flags; size_t n;
    assert(recvfds(p[1],fds,&n,CMSG_SPACE(sizeof(int)),buf,sizeof buf,MSG_WAITALL,&flags)==12 && n==1);
    assert(!memcmp(buf,"beforerights",12)); close(fds[0]);
    assert(read(p[1],buf,sizeof buf)==5 && !memcmp(buf,"after",5));
    assert(sendfds(p[0],&fd,1,"abc")==3);
    assert(recvfds(p[1],fds,&n,CMSG_SPACE(sizeof(int)),buf,1,0,&flags)==1 && n==1); close(fds[0]);
    assert(recvfds(p[1],fds,&n,CMSG_SPACE(sizeof(int)),buf,2,0,&flags)==2 && n==0);
    close(fd); close(p[0]); close(p[1]);
    // Closing the receiver drops pending rights even when the sender survives.
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,p)==0); int pipefd[2]; assert(pipe(pipefd)==0);
    assert(sendfds(p[0],&pipefd[1],1,"x")==1); close(pipefd[1]); close(p[1]);
    assert(read(pipefd[0],buf,1)==0); close(pipefd[0]); close(p[0]);
    puts("SCM_RIGHTS: stream/datagram/seqpacket, shared offsets, fork, CLOEXEC, PEEK, truncation, barriers and descriptor lifetime: ok");
    return 0;
}
