// SPDX-License-Identifier: GPL-3.0-or-later
#define main compatibility_base_main
#include "io-uring.c"
#undef main

enum { ROUNDS=128 };
struct peer { int data[2],ack[2]; unsigned rounds; };
static void *produce(void *opaque) {
    struct peer *p=opaque;
    for(unsigned i=0;i<p->rounds;i++) {
        char byte=(char)('a'+i%26),ack=0;
        assert(write(p->data[1],&byte,1)==1);
        assert(read(p->ack[0],&ack,1)==1 && ack==byte);
    }
    return NULL;
}
static void byte_roundtrips(struct ring *r) {
    assert(!mkdir("/tmp",0777)||errno==EEXIST);
    struct peer p={.rounds=ROUNDS}; assert(!pipe(p.data) && !pipe(p.ack));
    int file=open("/tmp/uring-worker-file",O_CREAT|O_TRUNC|O_RDWR,0600);char initial[64]={0};assert(file>=0&&write(file,initial,sizeof initial)==sizeof initial);
    pthread_t thread; assert(!pthread_create(&thread,NULL,produce,&p));
    for(unsigned i=0;i<ROUNDS;i++) {
        char byte=0;struct iovec v={&byte,1};
        if(i%16==0) {
            struct io_uring_sqe *s=add(r,IORING_OP_POLL_ADD,p.data[0],NULL,0,0,4000+i,0);s->poll32_events=POLLIN;
            assert(enter(r,1,1)==1);expect(r,4000+i,POLLIN);
        }
        if(i%2) {
            add(r,IORING_OP_READV,p.data[0],&v,1,0,1000+i,0);
        } else add(r,IORING_OP_READ,p.data[0],&byte,1,0,1000+i,0);
        assert(enter(r,1,1)==1); expect(r,1000+i,1); assert(byte=='a'+i%26);
        if(i%16==0) {
            char copy=0;add(r,IORING_OP_WRITE,file,&byte,1,i%64,5000+i,0);assert(enter(r,1,1)==1);expect(r,5000+i,1);
            add(r,IORING_OP_READ,file,&copy,1,i%64,6000+i,0);assert(enter(r,1,1)==1);expect(r,6000+i,1);assert(copy==byte);
        }
        if(i%2) {
            add(r,IORING_OP_WRITEV,p.ack[1],&v,1,0,2000+i,0);
        } else add(r,IORING_OP_WRITE,p.ack[1],&byte,1,0,2000+i,0);
        assert(enter(r,1,1)==1); expect(r,2000+i,1);
    }
    assert(!pthread_join(thread,NULL));
    assert(lseek(file,0,SEEK_CUR)==64);assert(!close(file));
    for(unsigned i=0;i<2;i++) assert(!close(p.data[i])&&!close(p.ack[i]));
    puts("uring worker scalar/vector/file-offset/poll/park/wake/retained-submit OK");
}
struct socket_wait { int pipe[2]; };
static void *wait_for_socket(void *opaque) {
    struct socket_wait *w=opaque;char byte;assert(read(w->pipe[0],&byte,1)==1);return NULL;
}
static void socket_roundtrip(struct ring *r,const char *version,const char *port) {
    int family=!strcmp(version,"6")?AF_INET6:AF_INET;
    int fd=socket(family,SOCK_STREAM,0); assert(fd>=0);
    if(family==AF_INET) {
        struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons((uint16_t)atoi(port))};
        assert(inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr)==1);
        assert(!connect(fd,(void *)&addr,sizeof addr));
    } else {
        struct sockaddr_in6 addr={.sin6_family=AF_INET6,.sin6_port=htons((uint16_t)atoi(port))};
        assert(inet_pton(AF_INET6,"::1",&addr.sin6_addr)==1);
        assert(!connect(fd,(void *)&addr,sizeof addr));
    }
    struct socket_wait w;assert(!pipe(w.pipe));pthread_t thread;assert(!pthread_create(&thread,NULL,wait_for_socket,&w));
    add(r,IORING_OP_SEND,fd,"ping",4,0,3000,0);assert(enter(r,1,1)==1);expect(r,3000,4);
    char data[4]={0};unsigned count=0;
    while(count<4) {
        add(r,IORING_OP_RECV,fd,data+count,4-count,0,3001+count,0);assert(enter(r,1,1)==1);
        struct io_uring_cqe c=next(r);assert(c.user_data==3001+count && c.res>0 && c.res<=(int)(4-count));count+=(unsigned)c.res;
    }
    assert(!memcmp(data,"pong",4));add(r,IORING_OP_RECV,fd,data,1,0,3010,0);assert(enter(r,1,1)==1);expect(r,3010,0);
    assert(write(w.pipe[1],"x",1)==1);assert(!pthread_join(thread,NULL));
    assert(!close(w.pipe[0])&&!close(w.pipe[1])&&!close(fd));puts("uring worker native TCP/host-events/EOF OK");
}
int main(int argc,char **argv) {
    struct ring r;init(&r,16);
    if(argc==3)socket_roundtrip(&r,argv[1],argv[2]);else byte_roundtrips(&r);
    finish(&r);puts("io-uring PASS");return 0;
}
