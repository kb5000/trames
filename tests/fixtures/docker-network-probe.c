// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static struct sockaddr_in endpoint(const char *ip,int port){struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(port)};assert(inet_pton(AF_INET,ip,&a.sin_addr)==1);return a;}
static int readable(int fd,int ms){struct pollfd p={.fd=fd,.events=POLLIN};int n=poll(&p,1,ms);assert(n>=0);return n;}
static int udp(const char *ip,int port){int fd=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);assert(fd>=0);struct sockaddr_in a=endpoint(ip,port);assert(!connect(fd,(void*)&a,sizeof a));return fd;}
static size_t skip_name(const unsigned char *r,size_t size,size_t p){for(int i=0;i<128;i++){assert(p<size);unsigned len=r[p++];if(!len)return p;if((len&0xc0)==0xc0){assert(p<size);return p+1;}assert(len<=63&&p+len<=size);p+=len;}assert(0);return 0;}
static void dns(const char *name,const char *ip,const char *expected){
 unsigned char q[512]={0x51,0x29,1,0,0,1},r[4096];size_t n=12;const char *p=name;
 while(*p){const char *end=strchr(p,'.');size_t len=end?(size_t)(end-p):strlen(p);assert(len&&len<=63&&n+len+6<sizeof q);q[n++]=len;memcpy(q+n,p,len);n+=len;if(!end)break;p=end+1;}
 q[n++]=0;q[n++]=0;q[n++]=1;q[n++]=0;q[n++]=1;int fd=udp(ip,53);assert(send(fd,q,n,0)==(ssize_t)n);assert(readable(fd,5000));ssize_t size=recv(fd,r,sizeof r,0);assert(size>=12&&r[0]==q[0]&&r[1]==q[1]&&(r[3]&15)==0&&(r[6]||r[7]));if(expected){struct in_addr target;assert(inet_pton(AF_INET,expected,&target)==1);size_t p=skip_name(r,size,12)+4;unsigned count=((unsigned)r[6]<<8)|r[7];int found=0;for(unsigned i=0;i<count;i++){p=skip_name(r,size,p);assert(p+10<=(size_t)size);unsigned type=((unsigned)r[p]<<8)|r[p+1],len=((unsigned)r[p+8]<<8)|r[p+9];p+=10;assert(p+len<=(size_t)size);if(type==1&&len==4&&!memcmp(r+p,&target,4))found=1;p+=len;}assert(found);}close(fd);printf("DNS_PASS %s via %s\n",name,ip);
}
static void client(const char *mode,const char *ip,int port){
 int blocked=!strcmp(mode,"blocked"),datagram=blocked||!strcmp(mode,"udp");int fd;
 if(datagram){fd=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);assert(fd>=0);struct sockaddr_in a=endpoint(ip,port);int result=connect(fd,(void*)&a,sizeof a);if(blocked&&result<0&&(errno==ENETUNREACH||errno==EACCES||errno==EHOSTUNREACH)){puts("ISOLATION_DROP_PASS");close(fd);return;}assert(!result);}else{fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);struct sockaddr_in a=endpoint(ip,port);assert(!connect(fd,(void*)&a,sizeof a));}
 const char *query=datagram?"TRAMES_UDP_NETWORK":"GET / HTTP/1.0\r\n\r\n";ssize_t sent=write(fd,query,strlen(query));if(blocked&&sent<0&&(errno==ENETUNREACH||errno==EACCES||errno==EHOSTUNREACH)){puts("ISOLATION_DROP_PASS");close(fd);return;}assert(sent==(ssize_t)strlen(query));
 if(blocked){assert(!readable(fd,300));puts("ISOLATION_DROP_PASS");close(fd);return;}
 assert(readable(fd,5000));char b[4096]={0};ssize_t n=read(fd,b,sizeof b-1);assert(n>0);if(datagram)assert((size_t)n==strlen(query)&&!memcmp(b,query,n));else{size_t used=n;while(!strstr(b,"TRAMES_FULL_NETWORK")&&used<sizeof b-1){assert(readable(fd,5000));n=read(fd,b+used,sizeof b-1-used);if(!n)break;assert(n>0);used+=n;b[used]=0;}assert(strstr(b,"TRAMES_FULL_NETWORK"));}close(fd);printf("%s_PASS %s:%d\n",datagram?"UDP":"TCP",ip,port);
}
static void serve(int port){
 struct sockaddr_in a=endpoint("0.0.0.0",port);int t=socket(AF_INET,SOCK_STREAM,0),u=socket(AF_INET,SOCK_DGRAM,0),one=1;assert(t>=0&&u>=0);assert(!setsockopt(t,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one));assert(!bind(t,(void*)&a,sizeof a)&&!listen(t,32)&&!bind(u,(void*)&a,sizeof a));puts("NETWORK_SERVICE_READY");fflush(stdout);struct pollfd p[2]={{.fd=t,.events=POLLIN},{.fd=u,.events=POLLIN}};
 for(;;){assert(poll(p,2,-1)>0);char b[4096];if(p[0].revents&POLLIN){int c=accept(t,0,0);assert(c>=0);ssize_t n=read(c,b,sizeof b);if(n>0){const char *r="HTTP/1.1 200 OK\r\nContent-Length: 20\r\nConnection: close\r\n\r\nTRAMES_FULL_NETWORK\n";assert(write(c,r,strlen(r))==(ssize_t)strlen(r));}close(c);}if(p[1].revents&POLLIN){socklen_t l=sizeof a;ssize_t n=recvfrom(u,b,sizeof b,0,(void*)&a,&l);assert(n>=0);assert(sendto(u,b,n,0,(void*)&a,l)==n);}}
}
int main(int argc,char **argv){
 if(argc>3&&!strcmp(argv[1],"ns")){char path[128];snprintf(path,sizeof path,"/proc/%d/ns/net",atoi(argv[2]));int fd=open(path,O_RDONLY);assert(fd>=0&&!setns(fd,CLONE_NEWNET));close(fd);argc-=2;argv+=2;}
 assert(argc>=3);if(!strcmp(argv[1],"rebind")){struct sockaddr_in a=endpoint("0.0.0.0",atoi(argv[2]));int t=socket(AF_INET,SOCK_STREAM,0),u=socket(AF_INET,SOCK_DGRAM,0),one=1;assert(t>=0&&u>=0);assert(!setsockopt(t,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one));assert(!bind(t,(void*)&a,sizeof a)&&!listen(t,1)&&!bind(u,(void*)&a,sizeof a));close(t);close(u);puts("PORT_REUSE_PASS");}else if(!strcmp(argv[1],"serve"))serve(atoi(argv[2]));else if(!strcmp(argv[1],"dns")){assert(argc==4||argc==5);dns(argv[2],argv[3],argc==5?argv[4]:NULL);}else{assert(argc==4);client(argv[1],argv[2],atoi(argv[3]));}return 0;
}
