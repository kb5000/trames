// SPDX-License-Identifier: GPL-3.0-or-later
#define main compatibility_base_main
#include "io-uring.c"
#undef main
enum { ROUNDS=128 };
struct peer {int input[2],ack[2];};
static void *produce(void *opaque) {
    struct peer *p=opaque;
    for(unsigned i=0;i<ROUNDS;i++) {
        char byte=(char)('a'+i%26),reply=0;assert(write(p->input[1],&byte,1)==1);
        assert(read(p->ack[0],&reply,1)==1&&reply==byte);
    }
    return NULL;
}
static void register_buffer(struct ring *r,char *memory) {
    struct iovec buffer={memory,128};assert(!syscall(SYS_io_uring_register,r->fd,IORING_REGISTER_BUFFERS,&buffer,1));
}
static void registered_roundtrips(struct ring *r,char *buffer) {
    assert(!mkdir("/tmp",0777)||errno==EEXIST);struct peer p;assert(!pipe(p.input)&&!pipe(p.ack));
    int file=open("/tmp/registered-original",O_CREAT|O_TRUNC|O_RDWR,0600);assert(file>=0);
    char seed[64];for(unsigned i=0;i<sizeof seed;i++)seed[i]=(char)('a'+i%26);
    assert(write(file,seed,sizeof seed)==sizeof seed&&lseek(file,0,SEEK_SET)==0);
    int files[]={p.input[0],p.ack[1],file,-1};assert(!syscall(SYS_io_uring_register,r->fd,IORING_REGISTER_FILES,files,4));
    assert(!close(p.input[0])&&!close(p.ack[1])&&!close(file));
    int replacements[3];for(unsigned i=0;i<3;i++) {
        char path[64];snprintf(path,sizeof path,"/tmp/registered-replacement-%u",i);
        replacements[i]=open(path,O_CREAT|O_TRUNC|O_RDWR,0600);assert(replacements[i]>=0);assert(write(replacements[i],"ZZZZZZZZ",8)==8);
    }
    assert(replacements[0]==files[0]&&replacements[1]==files[1]&&replacements[2]==files[2]);
    pthread_t thread;assert(!pthread_create(&thread,NULL,produce,&p));
    for(unsigned i=0;i<ROUNDS;i++) {
        uint64_t base=1000+i*16;unsigned count=0,expected=(1u<<0)|(1u<<1)|(1u<<15),seen=0;
        add(r,IORING_OP_READ_FIXED,0,buffer,1,0,base,IOSQE_FIXED_FILE|IOSQE_IO_LINK);count++;
        add(r,IORING_OP_WRITE_FIXED,1,buffer,1,0,base+1,IOSQE_FIXED_FILE|IOSQE_ASYNC);count++;
        if(i%8==0) {
            add(r,IORING_OP_READ_FIXED,2,buffer+2,1,2,base+2,IOSQE_FIXED_FILE|IOSQE_IO_LINK);count++;
            add(r,IORING_OP_WRITE_FIXED,2,buffer+2,1,3,base+3,IOSQE_FIXED_FILE);count++;expected|=(1u<<2)|(1u<<3);
        }
        if(i==0) {
            add(r,IORING_OP_READ,3,buffer+8,1,0,base+4,IOSQE_FIXED_FILE|IOSQE_IO_LINK);
            add(r,IORING_OP_NOP,-1,NULL,0,0,base+5,0);
            add(r,IORING_OP_READ,99,buffer+8,1,0,base+6,IOSQE_FIXED_FILE|IOSQE_IO_HARDLINK);
            add(r,IORING_OP_NOP,-1,NULL,0,0,base+7,0);
            struct io_uring_sqe *bad=add(r,IORING_OP_READ_FIXED,0,buffer+8,1,0,base+8,IOSQE_FIXED_FILE|IOSQE_IO_LINK);bad->buf_index=UINT16_MAX;
            add(r,IORING_OP_NOP,-1,NULL,0,0,base+9,0);count+=6;expected|=0x3f0;
        }
        add(r,IORING_OP_NOP,-1,NULL,0,0,base+15,IOSQE_IO_DRAIN);count++;assert(enter(r,count,count)==count);
        for(unsigned j=0;j<count;j++) {
            struct io_uring_cqe c=next(r);assert(c.user_data>=base&&c.user_data<base+16);
            unsigned tag=(unsigned)(c.user_data-base);assert(!(seen&(1u<<tag)));seen|=1u<<tag;
            int result=tag<4?1:tag==4||tag==6?-EBADF:tag==5||tag==9?-ECANCELED:tag==8?-EFAULT:0;
            assert(c.res==result);if(tag==15)assert(j+1==count);
        }
        assert(seen==expected&&buffer[0]=='a'+i%26);if(i%8==0)assert(buffer[2]=='c');
    }
    assert(!pthread_join(thread,NULL));assert(!close(p.input[1])&&!close(p.ack[0]));
    for(unsigned i=0;i<3;i++){char copy[8];assert(pread(replacements[i],copy,8,0)==8&&!memcmp(copy,"ZZZZZZZZ",8));assert(!close(replacements[i]));}
    file=open("/tmp/registered-original",O_RDONLY);char copy[4];assert(file>=0&&read(file,copy,4)==4&&!memcmp(copy,"abcc",4));assert(!close(file));
    puts("uring worker registered files/buffers/link/hardlink/drain/short-errors/reuse OK");
}
struct socket_wait {int pipe[2];};
static void *wait_for_socket(void *opaque) {struct socket_wait *w=opaque;char byte;assert(read(w->pipe[0],&byte,1)==1);return NULL;}
static void registered_tcp(struct ring *r,char *buffer,const char *version,const char *port) {
    int family=!strcmp(version,"6")?AF_INET6:AF_INET;int socketfd=socket(family,SOCK_STREAM,0);assert(socketfd>=0);
    if(family==AF_INET) {
        struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((uint16_t)atoi(port))};assert(inet_pton(AF_INET,"127.0.0.1",&a.sin_addr)==1);assert(!connect(socketfd,(void *)&a,sizeof a));
    } else {
        struct sockaddr_in6 a={.sin6_family=AF_INET6,.sin6_port=htons((uint16_t)atoi(port))};assert(inet_pton(AF_INET6,"::1",&a.sin6_addr)==1);assert(!connect(socketfd,(void *)&a,sizeof a));
    }
    assert(!syscall(SYS_io_uring_register,r->fd,IORING_REGISTER_FILES,&socketfd,1));assert(!close(socketfd));
    struct socket_wait w;assert(!pipe(w.pipe));pthread_t thread;assert(!pthread_create(&thread,NULL,wait_for_socket,&w));
    memcpy(buffer,"ping",4);
    add(r,IORING_OP_WRITE_FIXED,0,buffer,4,0,3000,IOSQE_FIXED_FILE|IOSQE_IO_LINK);
    add(r,IORING_OP_READ_FIXED,0,buffer+16,4,0,3001,IOSQE_FIXED_FILE);
    assert(enter(r,2,2)==2);expect(r,3000,4);struct io_uring_cqe c=next(r);assert(c.user_data==3001&&c.res>0&&c.res<=4);unsigned count=(unsigned)c.res;
    while(count<4) {
        add(r,IORING_OP_READ_FIXED,0,buffer+16+count,4-count,0,3001+count,IOSQE_FIXED_FILE);assert(enter(r,1,1)==1);c=next(r);assert(c.user_data==3001+count&&c.res>0&&c.res<=(int)(4-count));count+=(unsigned)c.res;
    }
    assert(!memcmp(buffer+16,"pong",4));add(r,IORING_OP_READ_FIXED,0,buffer+16,1,0,3010,IOSQE_FIXED_FILE);assert(enter(r,1,1)==1);expect(r,3010,0);
    assert(write(w.pipe[1],"x",1)==1);assert(!pthread_join(thread,NULL));assert(!close(w.pipe[0])&&!close(w.pipe[1]));
    assert(!syscall(SYS_io_uring_register,r->fd,IORING_UNREGISTER_FILES,NULL,0));puts("uring worker registered TCP/link/cross-cohort wait/EOF OK");
}
int main(int argc,char **argv) {
    struct ring r;init(&r,16);char *pages=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(pages!=MAP_FAILED);char *buffer=pages+4094;register_buffer(&r,buffer);
    if(argc==3)registered_tcp(&r,buffer,argv[1],argv[2]);else registered_roundtrips(&r,buffer);
    assert(!syscall(SYS_io_uring_register,r.fd,IORING_UNREGISTER_BUFFERS,NULL,0));finish(&r);assert(!munmap(pages,8192));puts("io-uring PASS");return 0;
}
