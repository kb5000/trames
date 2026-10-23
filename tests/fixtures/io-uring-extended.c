// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse only the raw UAPI queue helpers; the earlier suite is not re-run here.
#define main compatibility_base_main
#include "io-uring.c"
#undef main
#include <linux/openat2.h>
#include <time.h>
static int result_fd(struct ring *r,uint64_t data) {
    struct io_uring_cqe c=next(r); assert(c.user_data==data && c.res>=0); return c.res;
}
static void file_ops(struct ring *r) {
    char name[]="/tmp/uring-extended";
    struct io_uring_sqe *s=add(r,IORING_OP_OPENAT,AT_FDCWD,name,0600,0,100,0); s->open_flags=O_RDWR|O_CREAT|O_TRUNC|O_CLOEXEC;
    assert(enter(r,1,1)==1); int fd=result_fd(r,100); assert(fcntl(fd,F_GETFD)&FD_CLOEXEC);
    add(r,IORING_OP_FALLOCATE,fd,(void *)(uintptr_t)8192,0,0,101,0);
    assert(enter(r,1,1)==1); expect(r,101,0); struct stat st; assert(!fstat(fd,&st) && st.st_size==8192);
    add(r,IORING_OP_FTRUNCATE,fd,NULL,0,7,102,0); assert(enter(r,1,1)==1); expect(r,102,0);
    struct statx sx={0}; s=add(r,IORING_OP_STATX,fd,"",STATX_BASIC_STATS,(uintptr_t)&sx,103,0); s->statx_flags=AT_EMPTY_PATH;
    assert(enter(r,1,1)==1); expect(r,103,0); assert(sx.stx_size==7 && (sx.stx_mode&S_IFMT)==S_IFREG);
    add(r,IORING_OP_CLOSE,fd,NULL,0,0,104,0); assert(enter(r,1,1)==1); expect(r,104,0); assert(fcntl(fd,F_GETFD)==-1 && errno==EBADF);
    struct open_how how={.flags=O_RDONLY|O_CLOEXEC}; s=add(r,IORING_OP_OPENAT2,-99,name,sizeof how,(uintptr_t)&how,105,0);
    assert(enter(r,1,1)==1); fd=result_fd(r,105); assert(!fstat(fd,&st) && st.st_size==7); assert(!close(fd));
    how.resolve=RESOLVE_BENEATH; add(r,IORING_OP_OPENAT2,AT_FDCWD,name,sizeof how,(uintptr_t)&how,106,0);
    assert(enter(r,1,1)==1); expect(r,106,-EXDEV);
    add(r,IORING_OP_CLOSE,r->fd,NULL,0,0,107,0); assert(enter(r,1,1)==1); expect(r,107,-EBADF);
    puts("uring openat/openat2/fallocate/ftruncate/statx/close OK");
}
static void drain_and_path(struct ring *r) {
    int p[2]; assert(!pipe(p)); char c=0;
    add(r,IORING_OP_READ,p[0],&c,1,0,110,0);
    char name[]="/tmp/uring-captured";
    struct io_uring_sqe *s=add(r,IORING_OP_OPENAT,AT_FDCWD,name,0600,0,111,IOSQE_IO_DRAIN); s->open_flags=O_RDWR|O_CREAT;
    assert(enter(r,2,0)==2); assert(*r->cq_head==*r->cq_tail);
    memset(name,'x',sizeof(name)-1);
    add(r,IORING_OP_NOP,-1,NULL,0,0,112,0); assert(enter(r,1,0)==1); assert(*r->cq_head==*r->cq_tail);
    assert(write(p[1],"d",1)==1); assert(enter(r,0,3)==0); expect(r,110,1); int fd=result_fd(r,111); expect(r,112,0);
    assert(!close(fd) && c=='d'); assert(access("/tmp/uring-captured",F_OK)==0);
    assert(!close(p[0]) && !close(p[1])); puts("uring cross-enter drain/path import OK");
}
static void retained_directory(struct ring *r) {
    assert(!mkdir("/tmp/uring-dir",0700)); int dir=open("/tmp/uring-dir",O_RDONLY|O_DIRECTORY); assert(dir>=0);
    int p[2]; assert(!pipe(p)); char out;
    add(r,IORING_OP_READ,p[0],&out,1,0,113,0);
    add(r,IORING_OP_OPENAT,dir,"retained",0600,0,114,IOSQE_IO_DRAIN)->open_flags=O_RDWR|O_CREAT;
    assert(enter(r,2,0)==2); assert(!close(dir));
    int replacement=open("/tmp/uring-dir-replacement",O_RDWR|O_CREAT,0600); assert(replacement==dir);
    assert(write(p[1],"x",1)==1); assert(enter(r,0,2)==0); expect(r,113,1); int fd=result_fd(r,114);
    assert(access("/tmp/uring-dir/retained",F_OK)==0 && fcntl(replacement,F_GETFD)>=0);
    assert(write(fd,"magic",5)==5);
    int proc=open("/proc/self/fd",O_RDONLY|O_DIRECTORY); assert(proc>=0); char number[24]; snprintf(number,sizeof number,"%d",fd);
    add(r,IORING_OP_OPENAT,proc,number,0,0,115,0)->open_flags=O_RDONLY;
    assert(enter(r,1,1)==1); int duplicate=result_fd(r,115); char text[5]; assert(read(duplicate,text,5)==5 && !memcmp(text,"magic",5));
    assert(!close(duplicate) && !close(proc) && !close(fd) && !close(replacement) && !close(p[0]) && !close(p[1]));
    puts("uring retained dirfd/reuse/proc-fd path visibility OK");
}
static void advanced_timers(struct ring *r) {
    struct timespec now; assert(!clock_gettime(CLOCK_MONOTONIC,&now));
    struct __kernel_timespec ts={.tv_sec=now.tv_sec,.tv_nsec=now.tv_nsec};
    struct io_uring_sqe *s=add(r,IORING_OP_TIMEOUT,-1,&ts,1,0,120,0); s->timeout_flags=IORING_TIMEOUT_ABS;
    assert(enter(r,1,1)==1); expect(r,120,-ETIME);
    ts.tv_sec=3600; ts.tv_nsec=0;
    add(r,IORING_OP_TIMEOUT,-1,&ts,1,2,121,0); add(r,IORING_OP_NOP,-1,NULL,0,0,122,0); add(r,IORING_OP_NOP,-1,NULL,0,0,123,0);
    assert(enter(r,3,3)==3); expect(r,122,0); expect(r,123,0); expect(r,121,0);
    add(r,IORING_OP_TIMEOUT,-1,&ts,1,0,124,0); assert(enter(r,1,0)==1);
    ts.tv_sec=0; ts.tv_nsec=1000000;
    s=add(r,IORING_OP_TIMEOUT_REMOVE,-1,(void *)(uintptr_t)124,0,(uintptr_t)&ts,125,0); s->timeout_flags=IORING_TIMEOUT_UPDATE;
    assert(enter(r,1,2)==1); expect(r,125,0); expect(r,124,-ETIME);
    s=add(r,IORING_OP_TIMEOUT,-1,&ts,1,0,126,0); s->timeout_flags=IORING_TIMEOUT_ETIME_SUCCESS;
    assert(enter(r,1,1)==1); expect(r,126,0);
    int p[2]; assert(!pipe(p)); char c;
    add(r,IORING_OP_READ,p[0],&c,1,0,127,IOSQE_IO_LINK);
    add(r,IORING_OP_LINK_TIMEOUT,-1,&ts,1,0,128,0);
    assert(enter(r,2,2)==2); expect(r,127,-ECANCELED); expect(r,128,-ETIME);
    add(r,IORING_OP_NOP,-1,NULL,0,0,129,IOSQE_IO_LINK);
    add(r,IORING_OP_LINK_TIMEOUT,-1,&ts,1,0,130,IOSQE_IO_LINK);
    add(r,IORING_OP_NOP,-1,NULL,0,0,131,0);
    assert(enter(r,3,3)==3); expect(r,129,0); expect(r,130,-ECANCELED); expect(r,131,0);
    assert(!close(p[0]) && !close(p[1])); puts("uring absolute/count/update/link/ETIME_SUCCESS timers OK");
}
static void cancel_many(struct ring *r) {
    int p[2],q[2]; assert(!pipe(p) && !pipe(q)); char out[3]={0};
    add(r,IORING_OP_READ,p[0],&out[0],1,0,140,0); add(r,IORING_OP_READ,p[0],&out[1],1,0,141,0);
    add(r,IORING_OP_READ,q[0],&out[2],1,0,142,0); assert(enter(r,3,0)==3);
    struct io_uring_sqe *s=add(r,IORING_OP_ASYNC_CANCEL,p[0],NULL,0,0,143,0); s->cancel_flags=IORING_ASYNC_CANCEL_FD|IORING_ASYNC_CANCEL_ALL;
    assert(enter(r,1,3)==1); expect(r,140,-ECANCELED); expect(r,141,-ECANCELED); expect(r,143,2);
    s=add(r,IORING_OP_ASYNC_CANCEL,-1,NULL,0,0,144,0); s->cancel_flags=IORING_ASYNC_CANCEL_ANY;
    assert(enter(r,1,2)==1); expect(r,142,-ECANCELED); expect(r,144,1);
    assert(!close(p[0]) && !close(p[1]) && !close(q[0]) && !close(q[1])); puts("uring cancel-by-OFD/all/any OK");
}
static void fixed_buffers(struct ring *r) {
    char *data=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(data!=MAP_FAILED);
    memcpy(data+4094,"abcd",4); struct iovec vec={data+4094,4};
    assert(!syscall(SYS_io_uring_register,r->fd,IORING_REGISTER_BUFFERS,&vec,1));
    int fd=open("/tmp/uring-fixed",O_RDWR|O_CREAT|O_TRUNC,0600); assert(fd>=0);
    pid_t child=fork(); assert(child>=0);
    if(!child) { assert(!syscall(SYS_io_uring_register,r->fd,IORING_UNREGISTER_BUFFERS,NULL,0)); _exit(0); }
    // Unregister in another ring holder drops the table, not accepted pins.
    int status; assert(waitpid(child,&status,0)==child && status==0);
    assert(!syscall(SYS_io_uring_register,r->fd,IORING_REGISTER_BUFFERS,&vec,1));
    int p[2]; assert(!pipe(p));
    add(r,IORING_OP_READ_FIXED,p[0],data+4094,4,0,170,0); assert(enter(r,1,0)==1);
    assert(!syscall(SYS_io_uring_register,r->fd,IORING_UNREGISTER_BUFFERS,NULL,0));
    // This request still owns the original pages after unregister and remap.
    assert(!munmap(data,8192)); char *replacement=mmap(data,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0); assert(replacement==data);
    memcpy(replacement+4094,"xxxx",4); assert(write(p[1],"keep",4)==4);
    assert(enter(r,0,1)==0); expect(r,170,4); assert(!memcmp(replacement+4094,"xxxx",4));
    vec.iov_base=replacement+4094; assert(!syscall(SYS_io_uring_register,r->fd,IORING_REGISTER_BUFFERS,&vec,1));
    child=fork(); assert(child>=0);
    if(!child) { memcpy(replacement+4094,"fork",4); _exit(0); }
    assert(waitpid(child,&status,0)==child && status==0); assert(!memcmp(replacement+4094,"xxxx",4));
    memcpy(replacement+4094,"live",4);
    add(r,IORING_OP_WRITE_FIXED,fd,replacement+4094,4,0,171,0); assert(enter(r,1,1)==1); expect(r,171,4);
    char out[4]; assert(pread(fd,out,4,0)==4 && !memcmp(out,"live",4));
    add(r,IORING_OP_READ_FIXED,fd,replacement+4095,4,0,172,0); assert(enter(r,1,1)==1); expect(r,172,-EFAULT);
    assert(!syscall(SYS_io_uring_register,r->fd,IORING_UNREGISTER_BUFFERS,NULL,0));
    assert(!munmap(replacement,8192) && !close(fd) && !close(p[0]) && !close(p[1]));
    puts("uring fixed buffers/cross-page/unregister-pending/fork/remap OK");
}
static void extended_wait(struct ring *r) {
    assert(r->p.features&IORING_FEAT_EXT_ARG);
    struct __kernel_timespec ts={.tv_nsec=1000000};
    struct io_uring_getevents_arg args={.ts=(uintptr_t)&ts};
    assert(syscall(SYS_io_uring_enter,r->fd,0,1,IORING_ENTER_GETEVENTS|IORING_ENTER_EXT_ARG,&args,sizeof args)==-1 && errno==ETIME);
    add(r,IORING_OP_NOP,-1,NULL,0,0,180,0);
    assert(syscall(SYS_io_uring_enter,r->fd,1,2,IORING_ENTER_GETEVENTS|IORING_ENTER_EXT_ARG,&args,sizeof args)==1); expect(r,180,0);
    ts.tv_nsec=1000000000;
    add(r,IORING_OP_NOP,-1,NULL,0,0,181,0); unsigned head=*r->sq_head;
    assert(syscall(SYS_io_uring_enter,r->fd,1,1,IORING_ENTER_GETEVENTS|IORING_ENTER_EXT_ARG,&args,sizeof args)==-1 && errno==EINVAL);
    assert(*r->sq_head==head); assert(enter(r,1,1)==1); expect(r,181,0);
    puts("uring EXT_ARG wait timeout/partial-submit/validation OK");
}
static void local_ops(struct ring *r) {
    add(r,IORING_OP_SOCKET,AF_UNIX,NULL,0,SOCK_STREAM,150,0); assert(enter(r,1,1)==1); int listener=result_fd(r,150);
    struct sockaddr_un { unsigned short family; char path[108]; } address={.family=AF_UNIX,.path="/tmp/uring-socket"};
    add(r,IORING_OP_BIND,listener,&address,0,sizeof address,151,0);
    assert(enter(r,1,1)==1); expect(r,151,0);
    add(r,IORING_OP_LISTEN,listener,NULL,4,0,152,0); assert(enter(r,1,1)==1); expect(r,152,0);
    int client=socket(AF_UNIX,SOCK_STREAM,0); assert(client>=0);
    add(r,IORING_OP_ACCEPT,listener,NULL,0,0,153,0); assert(enter(r,1,0)==1);
    add(r,IORING_OP_CONNECT,client,&address,0,sizeof address,154,0); assert(enter(r,1,2)==1);
    int accepted=result_fd(r,154); assert(accepted==0); accepted=result_fd(r,153); assert(fcntl(accepted,F_GETFD)>=0);
    char out[4]={0}; add(r,IORING_OP_RECV,accepted,out,4,0,155,0); assert(enter(r,1,0)==1);
    add(r,IORING_OP_SEND,client,"data",4,0,156,0); assert(enter(r,1,2)==1); expect(r,156,4); expect(r,155,4); assert(!memcmp(out,"data",4));
    add(r,IORING_OP_RECV,accepted,out,1,0,157,0)->msg_flags=MSG_DONTWAIT; assert(enter(r,1,1)==1); expect(r,157,-EAGAIN);
    add(r,IORING_OP_SHUTDOWN,client,NULL,SHUT_WR,0,158,0); assert(enter(r,1,1)==1); expect(r,158,0);
    add(r,IORING_OP_RECV,accepted,out,1,0,159,0); assert(enter(r,1,1)==1); expect(r,159,0);
    assert(!close(accepted) && !close(client) && !close(listener)); puts("uring socket/bind/listen/accept/connect/send/recv/shutdown AF_UNIX OK");
}
static void tcp_ops(struct ring *r,const char *version,const char *port) {
    int family=!strcmp(version,"4")?AF_INET:AF_INET6;
    add(r,IORING_OP_SOCKET,family,NULL,0,SOCK_STREAM,160,0); assert(enter(r,1,1)==1); int fd=result_fd(r,160);
    union { struct sockaddr_in v4; struct sockaddr_in6 v6; } address={0};
    unsigned long number=strtoul(port,NULL,10); assert(number && number<=65535); unsigned len;
    if(family==AF_INET) { address.v4.sin_family=family; address.v4.sin_port=htons(number); assert(inet_pton(family,"127.0.0.1",&address.v4.sin_addr)==1); len=sizeof address.v4; }
    else { address.v6.sin6_family=family; address.v6.sin6_port=htons(number); assert(inet_pton(family,"::1",&address.v6.sin6_addr)==1); len=sizeof address.v6; }
    add(r,IORING_OP_CONNECT,fd,&address,0,len,161,0); assert(enter(r,1,1)==1); expect(r,161,0);
    add(r,IORING_OP_SEND,fd,"ping",4,0,162,0); assert(enter(r,1,1)==1); expect(r,162,4);
    char reply[4]; unsigned done=0;
    while(done<4) { add(r,IORING_OP_RECV,fd,reply+done,4-done,0,163,0); assert(enter(r,1,1)==1); struct io_uring_cqe c=next(r); assert(c.user_data==163 && c.res>0 && (unsigned)c.res<=4-done); done+=(unsigned)c.res; }
    assert(!memcmp(reply,"pong",4)); add(r,IORING_OP_RECV,fd,reply,1,0,164,0); assert(enter(r,1,1)==1); expect(r,164,0);
    assert(!close(fd)); puts("uring TCP connect/send/recv partial/EOF OK");
}
int main(int argc,char **argv) {
    assert(!mkdir("/tmp",0777) || errno==EEXIST); struct ring r; init(&r,8);
    if(argc==3) tcp_ops(&r,argv[1],argv[2]);
    else { assert(argc==1); file_ops(&r); drain_and_path(&r); retained_directory(&r); advanced_timers(&r); cancel_many(&r); fixed_buffers(&r); extended_wait(&r); local_ops(&r); }
    finish(&r); puts("io-uring PASS"); return 0;
}
