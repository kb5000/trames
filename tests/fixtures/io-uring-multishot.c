// SPDX-License-Identifier: GPL-3.0-or-later
#define main compatibility_base_main
#include "io-uring.c"
#undef main
#include <sys/un.h>
static struct io_uring_cqe take(struct ring *r) {
    unsigned head=*r->cq_head;
    assert(head!=atomic_load_explicit((_Atomic unsigned *)r->cq_tail,memory_order_acquire));
    struct io_uring_cqe c=r->cqes[head&(r->p.cq_entries-1)];
    atomic_store_explicit((_Atomic unsigned *)r->cq_head,head+1,memory_order_release); return c;
}
static void check(struct ring *r,uint64_t data,int res,unsigned flags) {
    struct io_uring_cqe c=take(r);
    if(c.user_data!=data || c.res!=res || c.flags!=flags) fprintf(stderr,"CQ: data=%llu res=%d flags=%x expected=%llu,%d,%x\n",(unsigned long long)c.user_data,c.res,c.flags,(unsigned long long)data,res,flags);
    assert(c.user_data==data && c.res==res && c.flags==flags);
}
static unsigned selected(unsigned id,unsigned more) { return IORING_CQE_F_BUFFER|(id<<IORING_CQE_BUFFER_SHIFT)|more; }
static void provide(struct ring *r,void *base,unsigned length,int count,unsigned id,unsigned group,uint64_t tag) {
    add(r,IORING_OP_PROVIDE_BUFFERS,count,base,length,id,tag,0)->buf_group=(uint16_t)group;
    assert(enter(r,1,1)==1); check(r,tag,0,0);
}
static void cancel(struct ring *r,uint64_t data,uint64_t tag) {
    add(r,IORING_OP_ASYNC_CANCEL,-1,(void *)(uintptr_t)data,0,0,tag,0);
    assert(enter(r,1,2)==1); check(r,data,-ECANCELED,0); check(r,tag,0,0);
}
static void classic(struct ring *r) {
    int p[2]; assert(!pipe(p)); char buffers[6][8]={{0}};
    provide(r,buffers,8,6,100,7,1);
    add(r,IORING_OP_READ,p[0],NULL,3,0,2,IOSQE_BUFFER_SELECT)->buf_group=7;
    assert(enter(r,1,0)==1); assert(*r->cq_head==*r->cq_tail);
    assert(write(p[1],"abc",3)==3); assert(enter(r,0,1)==0); check(r,2,3,selected(100,0)); assert(!memcmp(buffers[0],"abc",3));
    add(r,IORING_OP_REMOVE_BUFFERS,2,NULL,0,0,3,0)->buf_group=7; assert(enter(r,1,1)==1); check(r,3,2,0);
    assert(write(p[1],"def",3)==3); add(r,IORING_OP_READ,p[0],NULL,3,0,4,IOSQE_BUFFER_SELECT)->buf_group=7;
    assert(enter(r,1,1)==1); check(r,4,3,selected(103,0)); assert(!memcmp(buffers[3],"def",3));
    add(r,IORING_OP_REMOVE_BUFFERS,9,NULL,0,0,5,0)->buf_group=7; assert(enter(r,1,1)==1); check(r,5,2,0);
    add(r,IORING_OP_READ,p[0],NULL,1,0,6,IOSQE_BUFFER_SELECT)->buf_group=7; assert(enter(r,1,1)==1); check(r,6,-ENOBUFS,0);
    provide(r,(void *)UINT64_C(0x7fffffffffff),4,1,65535,8,7);
    assert(write(p[1],"x",1)==1); add(r,IORING_OP_READ,p[0],NULL,1,0,8,IOSQE_BUFFER_SELECT)->buf_group=8;
    assert(enter(r,1,1)==1); check(r,8,-EFAULT,selected(65535,0)); char x; assert(read(p[0],&x,1)==1 && x=='x');
    assert(!close(p[0]) && !close(p[1])); puts("provided buffers/select/remove/fault IDs OK");
}
static void multi_recv(struct ring *r) {
    int p[2]; assert(!socketpair(AF_UNIX,SOCK_STREAM,0,p)); char buffers[4][3]={{0}};
    provide(r,buffers,3,4,400,9,10);
    struct io_uring_sqe *s=add(r,IORING_OP_RECV,p[0],NULL,0,0,11,IOSQE_BUFFER_SELECT); s->buf_group=9; s->ioprio=IORING_RECV_MULTISHOT;
    assert(enter(r,1,0)==1); assert(*r->cq_head==*r->cq_tail);
    assert(write(p[1],"abcdef",6)==6); assert(enter(r,0,2)==0);
    check(r,11,3,selected(400,IORING_CQE_F_MORE)); check(r,11,3,selected(401,IORING_CQE_F_MORE));
    assert(!memcmp(buffers[0],"abc",3) && !memcmp(buffers[1],"def",3));
    assert(write(p[1],"g",1)==1); assert(enter(r,0,1)==0); check(r,11,1,selected(402,IORING_CQE_F_MORE));
    assert(!shutdown(p[1],SHUT_WR)); assert(enter(r,0,1)==0); check(r,11,0,selected(403,0));
    provide(r,buffers,3,1,500,9,12);
    s=add(r,IORING_OP_RECV,p[0],NULL,0,0,13,IOSQE_BUFFER_SELECT); s->buf_group=9; s->ioprio=IORING_RECV_MULTISHOT;
    assert(enter(r,1,1)==1); check(r,13,0,selected(500,0));
    assert(!close(p[0]) && !close(p[1])); puts("multishot Unix recv/partial/EOF OK");
}
static void exhausted_and_linked(struct ring *r) {
    int p[2]; assert(!socketpair(AF_UNIX,SOCK_STREAM,0,p)); char buffers[2][2]={{0}};
    provide(r,buffers,2,2,600,10,20);
    struct io_uring_sqe *s=add(r,IORING_OP_RECV,p[0],NULL,0,0,21,IOSQE_BUFFER_SELECT); s->buf_group=10; s->ioprio=IORING_RECV_MULTISHOT;
    assert(enter(r,1,0)==1); assert(write(p[1],"abcd",4)==4); assert(enter(r,0,3)==0);
    check(r,21,2,selected(600,IORING_CQE_F_MORE)); check(r,21,2,selected(601,IORING_CQE_F_MORE)); check(r,21,-ENOBUFS,0);
    provide(r,buffers,2,2,700,10,22);
    s=add(r,IORING_OP_RECV,p[0],NULL,0,0,23,IOSQE_BUFFER_SELECT|IOSQE_IO_LINK); s->buf_group=10; s->ioprio=IORING_RECV_MULTISHOT;
    add(r,IORING_OP_NOP,-1,NULL,0,0,24,0); assert(enter(r,2,0)==2);
    assert(write(p[1],"e",1)==1); assert(enter(r,0,1)==0); check(r,23,1,selected(700,IORING_CQE_F_MORE)); assert(*r->cq_head==*r->cq_tail);
    add(r,IORING_OP_ASYNC_CANCEL,-1,(void *)(uintptr_t)23,0,0,25,0); assert(enter(r,1,3)==1);
    check(r,23,-ECANCELED,0); check(r,25,0,0); check(r,24,-ECANCELED,0);
    assert(!close(p[0]) && !close(p[1])); puts("multishot ENOBUFS/rearm/cancel/link terminal OK");
}
static void multi_poll(struct ring *r) {
    int p[2]; assert(!pipe(p));
    add(r,IORING_OP_POLL_ADD,p[0],NULL,IORING_POLL_ADD_MULTI,0,30,0)->poll32_events=POLLIN;
    assert(enter(r,1,0)==1); assert(write(p[1],"x",1)==1); assert(enter(r,0,1)==0); check(r,30,POLLIN,IORING_CQE_F_MORE);
    for(int i=0;i<5;i++) assert(enter(r,0,0)==0);
    assert(*r->cq_head==*r->cq_tail);
    assert(write(p[1],"y",1)==1); assert(enter(r,0,1)==0); check(r,30,POLLIN,IORING_CQE_F_MORE);
    char out[2]; assert(read(p[0],out,2)==2); assert(enter(r,0,0)==0);
    struct io_uring_sqe *s=add(r,IORING_OP_POLL_REMOVE,-1,(void *)(uintptr_t)30,IORING_POLL_UPDATE_EVENTS|IORING_POLL_UPDATE_USER_DATA,31,32,0); s->poll32_events=POLLIN;
    assert(enter(r,1,1)==1); check(r,32,0,0);
    assert(write(p[1],"z",1)==1); assert(enter(r,0,1)==0); check(r,31,POLLIN,IORING_CQE_F_MORE);
    add(r,IORING_OP_POLL_REMOVE,-1,(void *)(uintptr_t)31,0,0,33,0); assert(enter(r,1,2)==1); check(r,31,-ECANCELED,0); check(r,33,0,0);
    assert(!close(p[0]) && !close(p[1])); puts("multishot poll edge/dedup/update/remove OK");
}
static void multi_accept(struct ring *r) {
    int listener=socket(AF_UNIX,SOCK_STREAM,0); assert(listener>=0);
    struct sockaddr_un address={.sun_family=AF_UNIX,.sun_path="/tmp/uring-multi"};
    assert(!bind(listener,(void *)&address,sizeof address) && !listen(listener,8));
    struct io_uring_sqe *s=add(r,IORING_OP_ACCEPT,listener,NULL,0,0,40,0); s->ioprio=IORING_ACCEPT_MULTISHOT; s->accept_flags=SOCK_CLOEXEC|SOCK_NONBLOCK;
    assert(enter(r,1,0)==1);
    int clients[2],accepted[2];
    for(int i=0;i<2;i++) { clients[i]=socket(AF_UNIX,SOCK_STREAM,0); assert(clients[i]>=0 && !connect(clients[i],(void *)&address,sizeof address)); }
    assert(enter(r,0,2)==0);
    for(int i=0;i<2;i++) { struct io_uring_cqe c=take(r); assert(c.user_data==40 && c.res>=0 && c.flags==IORING_CQE_F_MORE); accepted[i]=c.res; assert(fcntl(c.res,F_GETFD)&FD_CLOEXEC); assert(fcntl(c.res,F_GETFL)&O_NONBLOCK); }
    cancel(r,40,41); for(int i=0;i<2;i++) assert(!close(clients[i]) && !close(accepted[i])); assert(!close(listener));
    puts("multishot accept/new FD install/cancel OK");
}
struct multi_thread { struct ring *ring; int fd; };
static void *multi_submit_exit(void *arg) {
    struct multi_thread *t=arg;
    struct io_uring_sqe *s=add(t->ring,IORING_OP_RECV,t->fd,NULL,0,0,45,IOSQE_BUFFER_SELECT);
    s->buf_group=16; s->ioprio=IORING_RECV_MULTISHOT; assert(enter(t->ring,1,0)==1); return NULL;
}
static void multi_thread_exit(struct ring *r) {
    int p[2]; assert(!socketpair(AF_UNIX,SOCK_STREAM,0,p)); char buffers[2][2]={{0}};
    provide(r,buffers,2,2,750,16,44); struct multi_thread arg={r,p[0]}; pthread_t thread;
    assert(!pthread_create(&thread,NULL,multi_submit_exit,&arg)); assert(!pthread_join(thread,NULL));
    assert(enter(r,0,1)==0); check(r,45,-ECANCELED,0);
    assert(!close(p[0]) && !close(p[1])); puts("multishot submitting thread exit/final CQE OK");
}
static void ring_add(struct io_uring_buf_ring *br,unsigned entries,unsigned short tail,void *address,unsigned size,unsigned id) {
    br->bufs[tail&(entries-1)]=(struct io_uring_buf){.addr=(uintptr_t)address,.len=size,.bid=(uint16_t)id};
    atomic_store_explicit((_Atomic unsigned short *)&br->tail,(unsigned short)(tail+1),memory_order_release);
}
static unsigned ring_head(struct ring *r,unsigned group) {
    struct io_uring_buf_status status={.buf_group=group}; assert(!syscall(SYS_io_uring_register,r->fd,IORING_REGISTER_PBUF_STATUS,&status,1)); return status.head;
}
static void unregister_ring(struct ring *r,unsigned group) {
    struct io_uring_buf_reg reg={.bgid=(uint16_t)group}; assert(!syscall(SYS_io_uring_register,r->fd,IORING_UNREGISTER_PBUF_RING,&reg,1));
}
static void buffer_ring(struct ring *r) {
    struct io_uring_buf_ring *br=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(br!=MAP_FAILED);
    char data[3][2]={{0}}; ring_add(br,8,0,data[0],2,800); ring_add(br,8,1,data[1],2,801);
    struct io_uring_buf_reg reg={.ring_addr=(uintptr_t)br,.ring_entries=8,.bgid=11};
    assert(!syscall(SYS_io_uring_register,r->fd,IORING_REGISTER_PBUF_RING,&reg,1)); assert(ring_head(r,11)==0);
    int p[2]; assert(!socketpair(AF_UNIX,SOCK_STREAM,0,p));
    struct io_uring_sqe *s=add(r,IORING_OP_RECV,p[0],NULL,0,0,50,IOSQE_BUFFER_SELECT); s->buf_group=11; s->ioprio=IORING_RECV_MULTISHOT;
    assert(enter(r,1,0)==1); assert(write(p[1],"abcd",4)==4); assert(enter(r,0,3)==0);
    check(r,50,2,selected(800,IORING_CQE_F_MORE)); check(r,50,2,selected(801,IORING_CQE_F_MORE)); check(r,50,-ENOBUFS,0); assert(ring_head(r,11)==2);
    ring_add(br,8,2,data[2],2,802);
    s=add(r,IORING_OP_RECV,p[0],NULL,2,0,51,IOSQE_BUFFER_SELECT); s->buf_group=11;
    assert(enter(r,1,0)==1); assert(write(p[1],"ef",2)==2); assert(enter(r,0,1)==0); check(r,51,2,selected(802,0)); assert(ring_head(r,11)==3);
    assert(!memcmp(data[0],"ab",2) && !memcmp(data[1],"cd",2) && !memcmp(data[2],"ef",2));
    unregister_ring(r,11); assert(!munmap(br,4096));
    br=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(br!=MAP_FAILED); ring_add(br,8,0,data[0],2,900);
    reg=(struct io_uring_buf_reg){.ring_addr=(uintptr_t)br,.ring_entries=8,.bgid=12}; assert(!syscall(SYS_io_uring_register,r->fd,IORING_REGISTER_PBUF_RING,&reg,1));
    assert(!munmap(br,4096)); void *replacement=mmap(br,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0); assert(replacement==br);
    ring_add(br,8,0,(void *)UINT64_C(0x7fffffffffff),2,999);
    s=add(r,IORING_OP_RECV,p[0],NULL,2,0,52,IOSQE_BUFFER_SELECT); s->buf_group=12;
    assert(enter(r,1,0)==1); assert(write(p[1],"gh",2)==2); assert(enter(r,0,1)==0); check(r,52,2,selected(900,0)); assert(!memcmp(data[0],"gh",2));
    unregister_ring(r,12); assert(!munmap(replacement,4096)); assert(!close(p[0]) && !close(p[1]));
    puts("pbuf ring publish/status/replenish/unregister/remap identity OK");
}
static void datagrams_and_timers(struct ring *r) {
    int p[2]; assert(!socketpair(AF_UNIX,SOCK_DGRAM,0,p)); char data[4][2]={{0}};
    provide(r,data,2,4,1000,13,60);
    struct io_uring_sqe *s=add(r,IORING_OP_RECV,p[0],NULL,0,0,61,IOSQE_BUFFER_SELECT); s->buf_group=13; s->ioprio=IORING_RECV_MULTISHOT; s->msg_flags=MSG_TRUNC;
    assert(enter(r,1,0)==1); assert(send(p[1],"abcd",4,0)==4 && send(p[1],NULL,0,0)==0); assert(enter(r,0,2)==0);
    check(r,61,4,selected(1000,IORING_CQE_F_MORE)); check(r,61,0,selected(1001,IORING_CQE_F_MORE)); assert(!memcmp(data[0],"ab",2)); cancel(r,61,62);
    assert(!close(p[0]) && !close(p[1]));
    struct __kernel_timespec ts={0}; s=add(r,IORING_OP_TIMEOUT,-1,&ts,1,3,63,0); s->timeout_flags=IORING_TIMEOUT_MULTISHOT;
    assert(enter(r,1,3)==1); check(r,63,-ETIME,IORING_CQE_F_MORE); check(r,63,-ETIME,IORING_CQE_F_MORE); check(r,63,-ETIME,0);
    puts("multishot datagram/truncation/zero packet/finite timer OK");
}
static void pressure(void) {
    struct ring r; init(&r,2);
    struct io_uring_buf_ring *br=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(br!=MAP_FAILED);
    char data[32]={0}; for(unsigned i=0;i<32;i++) ring_add(br,32,(unsigned short)i,data+i,1,1100+i);
    struct io_uring_buf_reg reg={.ring_addr=(uintptr_t)br,.ring_entries=32,.bgid=14}; assert(!syscall(SYS_io_uring_register,r.fd,IORING_REGISTER_PBUF_RING,&reg,1));
    int p[2]; assert(!socketpair(AF_UNIX,SOCK_STREAM,0,p)); struct io_uring_sqe *s=add(&r,IORING_OP_RECV,p[0],NULL,0,0,70,IOSQE_BUFFER_SELECT); s->buf_group=14; s->ioprio=IORING_RECV_MULTISHOT;
    assert(enter(&r,1,0)==1); assert(write(p[1],"abcdefghijklmnopqrstuvwxyz",26)==26); assert(enter(&r,0,0)==0);
    unsigned before=ring_head(&r,14); assert(before>0 && before<=15); for(int i=0;i<3;i++) assert(enter(&r,0,0)==0); assert(ring_head(&r,14)==before);
    unsigned count=0;
    while(count<26) {
        if(*r.cq_head==*r.cq_tail) assert(enter(&r,0,1)==0);
        check(&r,70,1,selected(1100+count,IORING_CQE_F_MORE)); count++;
    }
    assert(!memcmp(data,"abcdefghijklmnopqrstuvwxyz",26)); cancel(&r,70,71);
    unregister_ring(&r,14); assert(!munmap(br,4096) && !close(p[0]) && !close(p[1])); finish(&r);
    puts("multishot CQ backpressure/buffer conservation/flag overflow OK");
}
static void tcp_multi(struct ring *r,const char *version,const char *port) {
    int family=!strcmp(version,"4")?AF_INET:AF_INET6; int fd=socket(family,SOCK_STREAM,0); assert(fd>=0);
    unsigned long n=strtoul(port,NULL,10); assert(n && n<=65535);
    if(family==AF_INET) { struct sockaddr_in a={.sin_family=family,.sin_port=htons((uint16_t)n)}; assert(inet_pton(family,"127.0.0.1",&a.sin_addr)==1 && !connect(fd,(void *)&a,sizeof a)); }
    else { struct sockaddr_in6 a={.sin6_family=family,.sin6_port=htons((uint16_t)n)}; assert(inet_pton(family,"::1",&a.sin6_addr)==1 && !connect(fd,(void *)&a,sizeof a)); }
    char buffers[8][2]={{0}},reply[4]={0}; provide(r,buffers,2,8,1200,15,80);
    struct io_uring_sqe *s=add(r,IORING_OP_RECV,fd,NULL,0,0,81,IOSQE_BUFFER_SELECT); s->buf_group=15; s->ioprio=IORING_RECV_MULTISHOT;
    assert(enter(r,1,0)==1); assert(write(fd,"ping",4)==4); unsigned done=0,shots=0;
    for(;;) {
        assert(enter(r,0,1)==0); struct io_uring_cqe c=take(r); assert(c.user_data==81 && c.res>=0 && c.res<=2);
        assert(c.flags==selected(1200+shots,c.res?IORING_CQE_F_MORE:0));
        if(!c.res) break;
        assert(done+(unsigned)c.res<=4); memcpy(reply+done,buffers[shots],(unsigned)c.res); done+=(unsigned)c.res; shots++;
    }
    assert(done==4 && !memcmp(reply,"pong",4)); assert(!close(fd)); puts("multishot TCP partial receive/EOF/original socket lifetime OK");
}
static void tcp_pressure(struct ring *r,const char *version,const char *port) {
    int family=version[0]=='4'?AF_INET:AF_INET6; int fd=socket(family,SOCK_STREAM,0); assert(fd>=0);
    unsigned long n=strtoul(port,NULL,10); assert(n && n<=65535);
    if(family==AF_INET) { struct sockaddr_in a={.sin_family=family,.sin_port=htons((uint16_t)n)}; assert(inet_pton(family,"127.0.0.1",&a.sin_addr)==1 && !connect(fd,(void *)&a,sizeof a)); }
    else { struct sockaddr_in6 a={.sin6_family=family,.sin6_port=htons((uint16_t)n)}; assert(inet_pton(family,"::1",&a.sin6_addr)==1 && !connect(fd,(void *)&a,sizeof a)); }
    char buffers[256]={0}; provide(r,buffers,1,256,2000,17,90);
    struct io_uring_sqe *s=add(r,IORING_OP_RECV,fd,NULL,0,0,91,IOSQE_BUFFER_SELECT); s->buf_group=17; s->ioprio=IORING_RECV_MULTISHOT;
    assert(enter(r,1,0)==1 && write(fd,"ping",4)==4 && enter(r,0,1)==0);
    // Leave CQ unread while the source remains ready. This must sleep rather
    // than repeatedly wake the owner on the backpressured native socket.
    assert(!usleep(150000));
    for(unsigned i=0;i<128;i++) {
        if(*r->cq_head==*r->cq_tail) assert(enter(r,0,1)==0);
        check(r,91,1,selected(2000+i,IORING_CQE_F_MORE));
        assert(buffers[i]==(char)('a'+i%26));
    }
    cancel(r,91,92); assert(!close(fd)); puts("multishot native TCP full-CQ idle wait/resume OK");
}

#ifndef TRAMES_MULTISHOT_EMBEDDED
int main(int argc,char **argv) {
    assert(!mkdir("/tmp",0777) || errno==EEXIST); struct ring r; init(&r,8);
    if(argc==3) { if(argv[1][1]=='p') tcp_pressure(&r,argv[1],argv[2]); else tcp_multi(&r,argv[1],argv[2]); }
    else { assert(argc==1); classic(&r); multi_recv(&r); exhausted_and_linked(&r); multi_poll(&r); multi_accept(&r); multi_thread_exit(&r); buffer_ring(&r); datagrams_and_timers(&r); pressure(); }
    finish(&r); puts("io-uring PASS"); return 0;
}

#endif
