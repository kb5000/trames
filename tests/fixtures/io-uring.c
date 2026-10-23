// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/io_uring.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
_Static_assert(sizeof(struct io_uring_sqe)==64,"SQE ABI");
_Static_assert(sizeof(struct io_uring_cqe)==16,"CQE ABI");
_Static_assert(sizeof(struct io_uring_params)==120,"params ABI");
struct ring { int fd; struct io_uring_params p; void *mem; size_t size; struct io_uring_sqe *sqes; unsigned *sq_head,*sq_tail,*cq_head,*cq_tail,*array; struct io_uring_cqe *cqes; };
static void init(struct ring *r,unsigned entries) {
    memset(r,0,sizeof *r); r->fd=(int)syscall(SYS_io_uring_setup,entries,&r->p); assert(r->fd>=0);
    assert(r->p.features&IORING_FEAT_SINGLE_MMAP); assert(fcntl(r->fd,F_GETFD)&FD_CLOEXEC);
    r->size=r->p.cq_off.cqes+r->p.cq_entries*sizeof(struct io_uring_cqe);
    r->mem=mmap(NULL,r->size,PROT_READ|PROT_WRITE,MAP_SHARED,r->fd,IORING_OFF_SQ_RING); assert(r->mem!=MAP_FAILED);
    r->sqes=mmap(NULL,r->p.sq_entries*64,PROT_READ|PROT_WRITE,MAP_SHARED,r->fd,IORING_OFF_SQES); assert(r->sqes!=MAP_FAILED);
    r->sq_head=(void *)((char *)r->mem+r->p.sq_off.head); r->sq_tail=(void *)((char *)r->mem+r->p.sq_off.tail);
    r->cq_head=(void *)((char *)r->mem+r->p.cq_off.head); r->cq_tail=(void *)((char *)r->mem+r->p.cq_off.tail);
    r->array=(void *)((char *)r->mem+r->p.sq_off.array); r->cqes=(void *)((char *)r->mem+r->p.cq_off.cqes);
}
static void finish(struct ring *r) {
    assert(!close(r->fd)); assert(!munmap(r->sqes,r->p.sq_entries*64)); assert(!munmap(r->mem,r->size));
}
static struct io_uring_sqe *add(struct ring *r,unsigned op,int fd,void *addr,unsigned len,uint64_t off,uint64_t data,unsigned flags) {
    unsigned tail=atomic_load_explicit((_Atomic unsigned *)r->sq_tail,memory_order_relaxed);
    assert(tail-*r->sq_head<r->p.sq_entries); unsigned slot=tail&(r->p.sq_entries-1);
    struct io_uring_sqe *s=&r->sqes[slot]; memset(s,0,sizeof *s); s->opcode=(unsigned char)op; s->fd=fd; s->addr=(uintptr_t)addr; s->len=len; s->off=off; s->user_data=data; s->flags=(unsigned char)flags;
    r->array[slot]=slot; atomic_store_explicit((_Atomic unsigned *)r->sq_tail,tail+1,memory_order_release); return s;
}
#ifndef TRAMES_URING_ENTER_TAP
#define TRAMES_URING_ENTER_TAP() ((void)0)
#endif
static long enter(struct ring *r,unsigned submit,unsigned min) { TRAMES_URING_ENTER_TAP(); return syscall(SYS_io_uring_enter,r->fd,submit,min,min?IORING_ENTER_GETEVENTS:0,NULL,0); }
static struct io_uring_cqe next(struct ring *r) {
    unsigned head=*r->cq_head; assert(head!=atomic_load_explicit((_Atomic unsigned *)r->cq_tail,memory_order_acquire));
    struct io_uring_cqe c=r->cqes[head&(r->p.cq_entries-1)]; atomic_store_explicit((_Atomic unsigned *)r->cq_head,head+1,memory_order_release); assert(!c.flags); return c;
}
static void expect(struct ring *r,uint64_t data,int result) { struct io_uring_cqe c=next(r); assert(c.user_data==data && c.res==result); }
static void basics(struct ring *r) {
    struct { struct io_uring_probe p; struct io_uring_probe_op ops[64]; } probe={0};
    assert(!syscall(SYS_io_uring_register,r->fd,IORING_REGISTER_PROBE,&probe,64));
    assert(probe.p.ops_len>=24 && (probe.ops[IORING_OP_READ].flags&IO_URING_OP_SUPPORTED));
    add(r,IORING_OP_NOP,-1,NULL,0,0,UINT64_C(0xdeadbeef12345678),0); assert(enter(r,1,1)==1); expect(r,UINT64_C(0xdeadbeef12345678),0);
    add(r,255,-1,NULL,0,0,10,IOSQE_IO_LINK); add(r,IORING_OP_NOP,-1,NULL,0,0,11,0);
    assert(enter(r,2,2)==2); expect(r,10,-EINVAL); expect(r,11,-ECANCELED);
    add(r,255,-1,NULL,0,0,12,IOSQE_IO_HARDLINK); add(r,IORING_OP_NOP,-1,NULL,0,0,13,0);
    assert(enter(r,2,2)==2); expect(r,12,-EINVAL); expect(r,13,0);
    assert(mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE,r->fd,0)==MAP_FAILED && errno==EINVAL);
    assert(mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,r->fd,4096)==MAP_FAILED && errno==EINVAL);
    puts("uring setup/mmap/probe/nop/link OK");
}
static void files(struct ring *r) {
    int fd=open("/tmp/uring-file",O_CREAT|O_TRUNC|O_RDWR,0600); assert(fd>=0); char out[32]={0};
    add(r,IORING_OP_WRITE,fd,"hello",5,42,20,IOSQE_IO_LINK);
    add(r,IORING_OP_READ,fd,out,5,42,21,0); assert(enter(r,2,2)==2); expect(r,20,5); expect(r,21,5); assert(!memcmp(out,"hello",5));
    assert(lseek(fd,0,SEEK_CUR)==0);
    struct iovec vec[2]={{"ab",2},{"cd",2}};
    add(r,IORING_OP_WRITEV,fd,vec,2,0,22,0); assert(enter(r,1,1)==1); expect(r,22,4);
    struct iovec readvec[2]={{out,1},{out+1,3}};
    add(r,IORING_OP_READV,fd,readvec,2,0,23,0); assert(enter(r,1,1)==1); expect(r,23,4); assert(!memcmp(out,"abcd",4));
    add(r,IORING_OP_READ,fd,out,2,UINT64_MAX,24,0); assert(enter(r,1,1)==1); expect(r,24,2); assert(lseek(fd,0,SEEK_CUR)==2);
    add(r,IORING_OP_READ,fd,(void *)UINT64_C(0x7fffffffffff),4,0,25,0); assert(enter(r,1,1)==1); expect(r,25,-EFAULT);
    assert(!syscall(SYS_io_uring_register,r->fd,IORING_REGISTER_FILES,&fd,1)); assert(!close(fd));
    int replacement=open("/tmp/uring-other",O_CREAT|O_TRUNC|O_RDWR,0600); assert(replacement==fd); assert(write(replacement,"xxxx",4)==4);
    add(r,IORING_OP_READ,0,out,4,0,26,IOSQE_FIXED_FILE); assert(enter(r,1,1)==1); expect(r,26,4); assert(!memcmp(out,"abcd",4));
    struct io_uring_files_update update={.offset=0,.fds=(uintptr_t)&replacement};
    assert(syscall(SYS_io_uring_register,r->fd,IORING_REGISTER_FILES_UPDATE,&update,1)==1);
    add(r,IORING_OP_READ,0,out,4,0,27,IOSQE_FIXED_FILE); assert(enter(r,1,1)==1); expect(r,27,4); assert(!memcmp(out,"xxxx",4));
    assert(!syscall(SYS_io_uring_register,r->fd,IORING_UNREGISTER_FILES,NULL,0));
    assert(!close(replacement)); puts("uring scalar/vector/offset/fixed-file/reuse OK");
}
static void events(struct ring *r) {
    int e=eventfd(0,EFD_NONBLOCK), p[2]; assert(e>=0 && !pipe(p));
    assert(!syscall(SYS_io_uring_register,r->fd,IORING_REGISTER_EVENTFD,&e,1));
    int ep=epoll_create1(0); assert(ep>=0); struct epoll_event event={.events=EPOLLIN,.data.u64=99}; assert(!epoll_ctl(ep,EPOLL_CTL_ADD,r->fd,&event));
    char out=0; add(r,IORING_OP_READ,p[0],&out,1,0,30,0); assert(enter(r,1,0)==1); assert(*r->cq_head==*r->cq_tail);
    int old=p[0]; assert(!close(p[0])); int other[2]; assert(!pipe(other) && other[0]==old);
    assert(write(p[1],"z",1)==1);
    assert(epoll_wait(ep,&event,1,2000)==1 && event.data.u64==99); expect(r,30,1); assert(out=='z');
    uint64_t count=0; assert(read(e,&count,8)==8 && count==1);
    assert(!close(other[0]) && !close(other[1]) && !close(p[1]));
    assert(!pipe(p)); struct io_uring_sqe *s=add(r,IORING_OP_POLL_ADD,p[0],NULL,0,0,31,0); s->poll32_events=POLLIN;
    assert(enter(r,1,0)==1); assert(write(p[1],"p",1)==1); assert(enter(r,0,1)==0); expect(r,31,POLLIN);
    char c; assert(read(p[0],&c,1)==1);
    s=add(r,IORING_OP_POLL_ADD,p[0],NULL,0,0,32,0); s->poll32_events=POLLIN;
    add(r,IORING_OP_POLL_REMOVE,-1,(void *)(uintptr_t)32,0,0,33,0);
    assert(enter(r,2,2)==2); expect(r,32,-ECANCELED); expect(r,33,0);
    assert(!close(p[0]) && !close(p[1]));
    assert(!syscall(SYS_io_uring_register,r->fd,IORING_UNREGISTER_EVENTFD,NULL,0)); assert(!close(e) && !close(ep));
    puts("uring async pipe/retention/epoll/eventfd/poll/cancel OK");
}
static void timers(struct ring *r) {
    struct __kernel_timespec ts={.tv_nsec=10000000};
    add(r,IORING_OP_TIMEOUT,-1,&ts,1,0,40,0); assert(enter(r,1,1)==1); expect(r,40,-ETIME);
    ts.tv_sec=3600; ts.tv_nsec=0;
    add(r,IORING_OP_TIMEOUT,-1,&ts,1,0,41,0);
    add(r,IORING_OP_TIMEOUT_REMOVE,-1,(void *)(uintptr_t)41,0,0,42,0);
    assert(enter(r,2,2)==2); expect(r,41,-ECANCELED); expect(r,42,0);
    add(r,IORING_OP_ASYNC_CANCEL,-1,(void *)(uintptr_t)0xffff,0,0,43,0); assert(enter(r,1,1)==1); expect(r,43,-ENOENT);
    puts("uring timeout/remove/cancel-miss OK");
}
static void overflow(void) {
    struct ring r; init(&r,2); assert(r.p.cq_entries==4);
    for(unsigned i=0;i<6;i+=2) { add(&r,IORING_OP_NOP,-1,NULL,0,0,50+i,0); add(&r,IORING_OP_NOP,-1,NULL,0,0,51+i,0); assert(enter(&r,2,0)==2); }
    expect(&r,50,0); expect(&r,51,0); assert(enter(&r,0,0)==0);
    for(unsigned i=52;i<56;i++) expect(&r,i,0);
    finish(&r); puts("uring CQ overflow preservation OK");
}
static void address_spaces(struct ring *r) {
    int p[2]; assert(!pipe(p)); volatile char out='?';
    add(r,IORING_OP_READ,p[0],(void *)&out,1,0,60,0); assert(enter(r,1,0)==1);
    pid_t child=fork(); assert(child>=0);
    if(!child) {
        assert(write(p[1],"a",1)==1);
        for(int i=0;i<20;i++) assert(!sched_yield());
        assert(out=='?'); _exit(0);
    }
    assert(enter(r,0,1)==0); expect(r,60,1); assert(out=='a');
    int status=0; assert(waitpid(child,&status,0)==child && status==0);
    assert(!close(p[0]) && !close(p[1])); puts("uring fork address-space ownership OK");
}
struct thread_request { struct ring *r; int fd; char data; };
static void *submit_then_exit(void *arg) {
    struct thread_request *t=arg;
    add(t->r,IORING_OP_READ,t->fd,&t->data,1,0,70,0);
    assert(enter(t->r,1,0)==1); return NULL;
}
static void thread_exit(struct ring *r) {
    int p[2]; assert(!pipe(p)); struct thread_request t={.r=r,.fd=p[0]}; pthread_t thread;
    assert(!pthread_create(&thread,NULL,submit_then_exit,&t)); assert(!pthread_join(thread,NULL));
    assert(enter(r,0,1)==0); expect(r,70,-ECANCELED);
    assert(!close(p[0]) && !close(p[1])); puts("uring thread-exit cancellation OK");
}
static void exec_cancel(struct ring *r) {
    int p[2]; assert(!pipe(p)); pid_t child=fork(); assert(child>=0);
    if(!child) {
        char out=0; add(r,IORING_OP_READ,p[0],&out,1,0,71,0); assert(enter(r,1,0)==1);
        assert(!fcntl(r->fd,F_SETFD,0));
        char *const args[]={"io-uring","exec-check",NULL}; execv("/proc/self/exe",args); perror("exec self"); _exit(99);
    }
    int status; assert(waitpid(child,&status,0)==child && status==0);
    assert(enter(r,0,1)==0); expect(r,71,-ECANCELED);
    assert(!close(p[0]) && !close(p[1])); puts("uring exec cancellation OK");
}
static void tcp(struct ring *r,const char *version,const char *port) {
    int family=!strcmp(version,"4")?AF_INET:AF_INET6; int fd=socket(family,SOCK_STREAM,0); assert(fd>=0);
    unsigned long number=strtoul(port,NULL,10); assert(number && number<=65535);
    if(family==AF_INET) {
        struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((uint16_t)number)};
        assert(inet_pton(family,"127.0.0.1",&a.sin_addr)==1); assert(!connect(fd,(void *)&a,sizeof a));
    } else {
        struct sockaddr_in6 a={.sin6_family=AF_INET6,.sin6_port=htons((uint16_t)number)};
        assert(inet_pton(family,"::1",&a.sin6_addr)==1); assert(!connect(fd,(void *)&a,sizeof a));
    }
    struct iovec parts[]={{"pi",2},{"ng",2}}; char reply[4]={0};
    add(r,IORING_OP_WRITEV,fd,parts,2,0,80,IOSQE_IO_LINK);
    add(r,IORING_OP_READ,fd,reply,4,0,81,0); assert(enter(r,2,2)==2);
    expect(r,80,4); struct io_uring_cqe c=next(r); assert(c.user_data==81 && c.res>0 && c.res<=4); unsigned done=(unsigned)c.res;
    while(done<4) { add(r,IORING_OP_READ,fd,reply+done,4-done,0,82,0); assert(enter(r,1,1)==1); c=next(r); assert(c.user_data==82 && c.res>0 && (unsigned)c.res<=4-done); done+=(unsigned)c.res; }
    assert(!memcmp(reply,"pong",4));
    add(r,IORING_OP_READ,fd,reply,1,0,83,0); assert(enter(r,1,1)==1); expect(r,83,0);
    assert(!close(fd)); puts("uring TCP scalar/vector/partial/EOF OK");
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"exec-check")) return 0;
    assert(!mkdir("/tmp",0777) || errno==EEXIST);
    struct io_uring_params bad={.flags=IORING_SETUP_SQPOLL};
    assert(syscall(SYS_io_uring_setup,8,&bad)==-1 && errno==EINVAL);
    struct ring r; init(&r,8);
    if(argc==3) tcp(&r,argv[1],argv[2]);
    else { assert(argc==1); basics(&r); files(&r); events(&r); timers(&r); overflow(); address_spaces(&r); thread_exit(&r); exec_cancel(&r); }
    finish(&r);
    puts("io-uring PASS"); return 0;
}
