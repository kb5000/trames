// SPDX-License-Identifier: GPL-3.0-or-later
static unsigned guest_enter_calls;
#define TRAMES_URING_ENTER_TAP() (++guest_enter_calls)
#define TRAMES_MULTISHOT_EMBEDDED
#include "io-uring-multishot.c"
#include <sys/epoll.h>
static void *park_peer(void *arg) { int *p=arg; char byte; assert(read(p[0],&byte,1)==1); return NULL; }
static void worker_batches(struct ring *r) {
    int pair[2]; assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair)); char buffers[3]={0};
    for(unsigned i=0;i<128;i++) {
        unsigned id=3000+i*3; uint64_t tag=10000+i*16;
        provide(r,buffers,1,3,id,19,tag);
        struct io_uring_sqe *s=add(r,IORING_OP_RECV,pair[0],NULL,0,0,tag+1,IOSQE_BUFFER_SELECT|IOSQE_IO_LINK);
        s->buf_group=19; s->ioprio=IORING_RECV_MULTISHOT;
        add(r,IORING_OP_NOP,-1,NULL,0,0,tag+2,0); assert(enter(r,2,0)==2);
        assert(write(pair[1],"ab",2)==2 && enter(r,0,2)==0);
        check(r,tag+1,1,selected(id,IORING_CQE_F_MORE)); check(r,tag+1,1,selected(id+1,IORING_CQE_F_MORE));
        assert(buffers[0]=='a' && buffers[1]=='b');
        add(r,IORING_OP_ASYNC_CANCEL,-1,(void *)(uintptr_t)(tag+1),0,0,tag+3,0); assert(enter(r,1,3)==1);
        check(r,tag+1,-ECANCELED,0); check(r,tag+3,0,0); check(r,tag+2,-ECANCELED,0);
        add(r,IORING_OP_REMOVE_BUFFERS,3,NULL,0,0,tag+4,0)->buf_group=19;
        assert(enter(r,1,1)==1); check(r,tag+4,1,0);
    }
    assert(!close(pair[0]) && !close(pair[1])); puts("worker 128 multishot/link/cancel/provided batches OK");
}
static void ext_arg_wait(struct ring *r) {
    struct __kernel_timespec ts={.tv_nsec=10000000}; struct io_uring_getevents_arg arg={.ts=(uintptr_t)&ts};
    guest_enter_calls++; errno=0; assert(syscall(SYS_io_uring_enter,r->fd,0,1,IORING_ENTER_GETEVENTS|IORING_ENTER_EXT_ARG,&arg,sizeof arg)==-1 && errno==ETIME);
    puts("worker EXT_ARG deadline OK");
}
static void tcp_cq_waits(struct ring *r,const char *version,const char *port) {
    int family=version[0]=='4'?AF_INET:AF_INET6; int fd=socket(family,SOCK_STREAM,0); assert(fd>=0);
    unsigned long n=strtoul(port,NULL,10); assert(n && n<=65535);
    if(family==AF_INET) { struct sockaddr_in a={.sin_family=family,.sin_port=htons((uint16_t)n)}; assert(inet_pton(family,"127.0.0.1",&a.sin_addr)==1 && !connect(fd,(void *)&a,sizeof a)); }
    else { struct sockaddr_in6 a={.sin6_family=family,.sin6_port=htons((uint16_t)n)}; assert(inet_pton(family,"::1",&a.sin6_addr)==1 && !connect(fd,(void *)&a,sizeof a)); }
    int epoll=epoll_create1(EPOLL_CLOEXEC); struct epoll_event event={.events=EPOLLIN|EPOLLET,.data.u64=777};
    assert(epoll>=0 && !epoll_ctl(epoll,EPOLL_CTL_ADD,r->fd,&event));
    char buffers[8][2]={{0}},reply[4]={0}; provide(r,buffers,2,8,1200,15,80);
    struct io_uring_sqe *s=add(r,IORING_OP_RECV,fd,NULL,0,0,81,IOSQE_BUFFER_SELECT); s->buf_group=15; s->ioprio=IORING_RECV_MULTISHOT;
    add(r,IORING_OP_SEND,fd,"ping",4,0,82,0); assert(enter(r,2,1)==2); check(r,82,4,0);
    unsigned done=0,shots=0;
    for(;;) {
        if(*r->cq_head==*r->cq_tail) {
            if(!shots) { struct pollfd p={.fd=r->fd,.events=POLLIN}; assert(poll(&p,1,2000)==1 && (p.revents&POLLIN)); }
            else { struct epoll_event out; assert(epoll_wait(epoll,&out,1,2000)==1 && (out.events&EPOLLIN) && out.data.u64==777); }
        }
        struct io_uring_cqe c=take(r); assert(c.user_data==81 && c.res>=0 && c.res<=2);
        assert(c.flags==selected(1200+shots,c.res?IORING_CQE_F_MORE:0)); if(!c.res) break;
        assert(done+(unsigned)c.res<=4); memcpy(reply+done,buffers[shots],(unsigned)c.res); done+=(unsigned)c.res; shots++;
    }
    assert(done==4 && !memcmp(reply,"pong",4)); add(r,IORING_OP_NOP,-1,NULL,0,0,83,0); assert(enter(r,1,1)==1); check(r,83,0,0);
    assert(!close(epoll) && !close(fd)); puts("worker CQ ppoll/epoll/native TCP/partial/EOF OK");
}
int main(int argc,char **argv) {
    // FD installation and task teardown remain covered by the original fixture.
    (void)multi_accept; (void)multi_thread_exit; (void)tcp_multi;
    assert(!mkdir("/tmp",0777) || errno==EEXIST); struct ring r; init(&r,8);
    // Initialize libc's stdout metadata path before the worker-only exercise.
    puts("worker async start");
    int peer[2]; assert(!pipe(peer)); pthread_t thread; assert(!pthread_create(&thread,NULL,park_peer,peer));
    unsigned local_registers=0;
    if(argc==3) { if(argv[1][1]=='p') tcp_pressure(&r,argv[1],argv[2]); else tcp_cq_waits(&r,argv[1],argv[2]); }
    else {
        assert(argc==1 || argc==2);
        if(argc==1 || !strcmp(argv[1],"classic")) classic(&r);
        if(argc==1 || !strcmp(argv[1],"recv")) multi_recv(&r);
        if(argc==1 || !strcmp(argv[1],"links")) exhausted_and_linked(&r);
        if(argc==1 || !strcmp(argv[1],"poll")) multi_poll(&r);
        if(argc==1 || !strcmp(argv[1],"buffers")) { buffer_ring(&r); local_registers+=5; }
        if(argc==1 || !strcmp(argv[1],"timers")) datagrams_and_timers(&r);
        if(argc==1 || !strcmp(argv[1],"pressure")) { pressure(); local_registers+=3; }
        if(argc==1 || !strcmp(argv[1],"batches")) worker_batches(&r);
        if(argc==1 || !strcmp(argv[1],"ext")) ext_arg_wait(&r);
    }
    // PBUF_STATUS/unregister controls are counted separately from enter calls.
    printf("expected-worker-uring: %u\n",guest_enter_calls+local_registers);
    finish(&r); assert(write(peer[1],"x",1)==1 && !pthread_join(thread,NULL)); assert(!close(peer[0]) && !close(peer[1]));
    puts("io-uring PASS"); return 0;
}
