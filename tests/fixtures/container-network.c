// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <net/if.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
struct packet {struct nlmsghdr h;unsigned char data[4096];};
static unsigned sequence;
static void begin(struct packet *p,unsigned kind,unsigned flags,size_t payload) {
    memset(p,0,sizeof(*p));p->h.nlmsg_len=NLMSG_LENGTH(payload);p->h.nlmsg_type=kind;p->h.nlmsg_flags=NLM_F_REQUEST|NLM_F_ACK|flags;p->h.nlmsg_seq=++sequence;
}
static void attribute(struct packet *p,unsigned type,const void *data,size_t n) {
    size_t offset=NLMSG_ALIGN(p->h.nlmsg_len);assert(offset+RTA_SPACE(n)<=sizeof(*p));
    struct rtattr *a=(void*)((char*)p+offset);a->rta_type=type;a->rta_len=RTA_LENGTH(n);memcpy(RTA_DATA(a),data,n);p->h.nlmsg_len=offset+RTA_SPACE(n);
}
static size_t nest(struct packet *p,unsigned type) {size_t n=NLMSG_ALIGN(p->h.nlmsg_len);attribute(p,type|NLA_F_NESTED,"",0);return n;}
static void end_nest(struct packet *p,size_t start) {((struct rtattr*)((char*)p+start))->rta_len=p->h.nlmsg_len-start;}
static void number(struct packet *p,unsigned type,unsigned value){attribute(p,type,&value,4);}
static int control(void){int s=socket(AF_NETLINK,SOCK_RAW,NETLINK_ROUTE);assert(s>=0);return s;}
static int send_packet(int fd,struct packet *p) {
    struct sockaddr_nl destination={.nl_family=AF_NETLINK};
    assert(sendto(fd,p,p->h.nlmsg_len,0,(void*)&destination,sizeof(destination))==(ssize_t)p->h.nlmsg_len);
    char data[32768];ssize_t n=recv(fd,data,sizeof(data),0);assert(n>=(ssize_t)NLMSG_LENGTH(sizeof(struct nlmsgerr)));
    struct nlmsghdr *h=(void*)data;assert(h->nlmsg_type==NLMSG_ERROR&&h->nlmsg_seq==p->h.nlmsg_seq);
    return ((struct nlmsgerr*)NLMSG_DATA(h))->error;
}
static unsigned index_of(const char *name) {int s=socket(AF_INET,SOCK_DGRAM,0);if(s<0)fprintf(stderr,"index_of(%s): socket errno=%d\n",name,errno);assert(s>=0);struct ifreq r={0};strcpy(r.ifr_name,name);assert(ioctl(s,SIOCGIFINDEX,&r)==0);close(s);return r.ifr_ifindex;}
static void up(unsigned index) {struct packet p;begin(&p,RTM_NEWLINK,0,sizeof(struct ifinfomsg));struct ifinfomsg *i=(void*)p.data;i->ifi_index=index;i->ifi_flags=IFF_UP;i->ifi_change=IFF_UP;int s=control();assert(send_packet(s,&p)==0);close(s);}
static void bridge(int s,const char *name) {struct packet p;begin(&p,RTM_NEWLINK,NLM_F_CREATE|NLM_F_EXCL,sizeof(struct ifinfomsg));attribute(&p,IFLA_IFNAME,name,strlen(name)+1);size_t info=nest(&p,IFLA_LINKINFO);attribute(&p,IFLA_INFO_KIND,"bridge",7);end_nest(&p,info);assert(send_packet(s,&p)==0);}
static void veth(int s,const char *name,int peer_fd) {
    struct packet p;begin(&p,RTM_NEWLINK,NLM_F_CREATE|NLM_F_EXCL,sizeof(struct ifinfomsg));attribute(&p,IFLA_IFNAME,name,strlen(name)+1);
    size_t info=nest(&p,IFLA_LINKINFO);attribute(&p,IFLA_INFO_KIND,"veth",5);size_t data=nest(&p,IFLA_INFO_DATA);size_t peer=nest(&p,VETH_INFO_PEER);
    memset((char*)&p+p.h.nlmsg_len,0,sizeof(struct ifinfomsg));p.h.nlmsg_len+=sizeof(struct ifinfomsg);
    attribute(&p,IFLA_IFNAME,"eth0",5);number(&p,IFLA_NET_NS_FD,peer_fd);end_nest(&p,peer);end_nest(&p,data);end_nest(&p,info);assert(send_packet(s,&p)==0);
}
static void master(int s,unsigned index,unsigned bridge) {struct packet p;begin(&p,RTM_NEWLINK,0,sizeof(struct ifinfomsg));((struct ifinfomsg*)p.data)->ifi_index=index;number(&p,IFLA_MASTER,bridge);assert(send_packet(s,&p)==0);}
static void address(unsigned index,const char *text,int family,unsigned prefix) {struct packet p;begin(&p,RTM_NEWADDR,NLM_F_CREATE|NLM_F_EXCL,sizeof(struct ifaddrmsg));struct ifaddrmsg *a=(void*)p.data;a->ifa_family=family;a->ifa_prefixlen=prefix;a->ifa_index=index;unsigned char bytes[16];assert(inet_pton(family,text,bytes)==1);attribute(&p,IFA_ADDRESS,bytes,family==AF_INET?4:16);attribute(&p,IFA_LOCAL,bytes,family==AF_INET?4:16);int s=control();assert(send_packet(s,&p)==0);close(s);}
static int space(void){assert(unshare(CLONE_NEWNET)==0);int fd=open("/proc/self/ns/net",O_RDONLY);assert(fd>=0);return fd;}
static void enter(int fd){assert(setns(fd,CLONE_NEWNET)==0);}
static void text_file(const char *path,const char *expected) {char data[256];int fd=open(path,O_RDONLY);assert(fd>=0);ssize_t n=read(fd,data,sizeof(data)-1);assert(n>=0);data[n]=0;assert(strstr(data,expected));close(fd);}
static struct sockaddr_in endpoint(const char *ip,int port) {struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(port)};assert(inet_pton(AF_INET,ip,&a.sin_addr)==1);return a;}
int main(void) {
    int root=open("/proc/self/ns/net",O_RDONLY);assert(root>=0);int old=control();
    int a=space();enter(root);int b=space();enter(root);int c=space();enter(root);
    bridge(old,"br-data");bridge(old,"br-other");unsigned br=index_of("br-data"),other=index_of("br-other");up(br);up(other);
    veth(old,"host-a",a);veth(old,"host-b",b);veth(old,"host-c",c);
    unsigned ha=index_of("host-a"),hb=index_of("host-b"),hc=index_of("host-c");master(old,ha,br);master(old,hb,br);master(old,hc,other);up(ha);up(hb);up(hc);
    enter(b);unsigned bi=index_of("eth0");up(bi);
    int listener=socket(AF_INET,SOCK_STREAM,0),receiver=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);assert(listener>=0&&receiver>=0);
    struct sockaddr_in any=endpoint("0.0.0.0",16401);assert(bind(listener,(void*)&any,sizeof(any))==0);assert(listen(listener,2)==0);any.sin_port=htons(16402);assert(bind(receiver,(void*)&any,sizeof(any))==0);
    address(bi,"192.0.2.20",AF_INET,24);text_file("/sys/class/net/eth0/type","1\n");text_file("/proc/net/route","eth0");
    enter(a);unsigned ai=index_of("eth0");up(ai);address(ai,"192.0.2.10",AF_INET,24);
    int client=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in dest=endpoint("192.0.2.20",16401);assert(connect(client,(void*)&dest,sizeof(dest))==0);assert(write(client,"routed",6)==6);
    enter(b);int accepted=accept(listener,NULL,NULL);assert(accepted>=0);char bytes[64];assert(read(accepted,bytes,sizeof(bytes))==6&&!memcmp(bytes,"routed",6));
    enter(a);int sender=socket(AF_INET,SOCK_DGRAM,0);dest.sin_port=htons(16402);assert(sendto(sender,"same",4,0,(void*)&dest,sizeof(dest))==4);
    enter(b);assert(recv(receiver,bytes,sizeof(bytes),0)==4&&!memcmp(bytes,"same",4));
    enter(c);unsigned ci=index_of("eth0");up(ci);address(ci,"192.0.2.30",AF_INET,24);int separate=socket(AF_INET,SOCK_DGRAM,0);assert(sendto(separate,"blocked",7,0,(void*)&dest,sizeof(dest))==7);
    enter(b);assert(recv(receiver,bytes,sizeof(bytes),0)==-1&&errno==EAGAIN);
    enter(root);master(old,hb,0);enter(a);assert(sendto(sender,"detached",8,0,(void*)&dest,sizeof(dest))==8);enter(b);assert(recv(receiver,bytes,sizeof(bytes),0)==-1&&errno==EAGAIN);
    // Original control FD still changes the root namespace after caller setns.
    master(old,hb,br);enter(a);assert(sendto(sender,"back",4,0,(void*)&dest,sizeof(dest))==4);enter(b);assert(recv(receiver,bytes,sizeof(bytes),0)==4&&!memcmp(bytes,"back",4));
    int handles[]={root,old,a,b,c,listener,receiver,client,accepted,sender,separate};for(unsigned i=0;i<sizeof(handles)/sizeof(handles[0]);i++)close(handles[i]);
    puts("container network: veth/bridge, namespace FDs, live TCP/UDP, sysfs/proc and cross-bridge isolation: ok");return 0;
}
