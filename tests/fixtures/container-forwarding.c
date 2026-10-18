// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the same unmodified netlink message helpers as the internal-link lab.
#define main previous_internal_network_case
#include "container-network.c"
#undef main
static void default_route(unsigned index,const char *gateway,int family) {
    struct packet p;begin(&p,RTM_NEWROUTE,NLM_F_CREATE|NLM_F_EXCL,sizeof(struct rtmsg));
    struct rtmsg *r=(void*)p.data;r->rtm_family=family;r->rtm_table=RT_TABLE_MAIN;r->rtm_protocol=RTPROT_BOOT;r->rtm_type=RTN_UNICAST;
    number(&p,RTA_OIF,index);unsigned char ip[16];assert(inet_pton(family,gateway,ip)==1);attribute(&p,RTA_GATEWAY,ip,family==AF_INET?4:16);int fd=control();assert(send_packet(fd,&p)==0);close(fd);
}
static void toggle(int fd,int enabled) {assert(lseek(fd,0,SEEK_SET)==0);assert(write(fd,enabled?"1\n":"0\n",2)==2);}
static struct sockaddr_in6 endpoint6(const char *ip,int port) {struct sockaddr_in6 a={.sin6_family=AF_INET6,.sin6_port=htons(port)};assert(inet_pton(AF_INET6,ip,&a.sin6_addr)==1);return a;}
int main(void) {
    int root=open("/proc/self/ns/net",O_RDONLY);assert(root>=0);int a=space();enter(root);int b=space();enter(root);
    int forward4=open("/proc/sys/net/ipv4/ip_forward",O_RDWR),forward6=open("/proc/sys/net/ipv6/conf/all/forwarding",O_RDWR);assert(forward4>=0&&forward6>=0);text_file("/proc/sys/net/ipv4/conf/all/forwarding","0\n");
    // A wildcard root service must acquire subsequently configured bridge IPs.
    int root_server=socket(AF_INET,SOCK_STREAM,0);assert(root_server>=0);struct sockaddr_in any=endpoint("0.0.0.0",0);assert(bind(root_server,(void*)&any,sizeof(any))==0);assert(listen(root_server,4)==0);socklen_t size=sizeof(any);assert(getsockname(root_server,(void*)&any,&size)==0);int port=ntohs(any.sin_port);
    int netlink=control();bridge(netlink,"routed-a");bridge(netlink,"routed-b");unsigned ba=index_of("routed-a"),bb=index_of("routed-b");up(ba);up(bb);address(ba,"172.26.0.1",AF_INET,24);address(bb,"172.27.0.1",AF_INET,24);address(ba,"fd26::1",AF_INET6,64);address(bb,"fd27::1",AF_INET6,64);
    veth(netlink,"route-pa",a);veth(netlink,"route-pb",b);unsigned pa=index_of("route-pa"),pb=index_of("route-pb");master(netlink,pa,ba);master(netlink,pb,bb);up(pa);up(pb);
    enter(a);unsigned ai=index_of("eth0");up(ai);address(ai,"172.26.0.2",AF_INET,24);address(ai,"fd26::2",AF_INET6,64);default_route(ai,"172.26.0.1",AF_INET);default_route(ai,"fd26::1",AF_INET6);
    int client=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in gateway=endpoint("172.26.0.1",port);assert(connect(client,(void*)&gateway,sizeof(gateway))==0);assert(write(client,"bridge",6)==6);
    enter(root);int accepted=accept(root_server,NULL,NULL);assert(accepted>=0);char bytes[64];assert(read(accepted,bytes,sizeof(bytes))==6&&!memcmp(bytes,"bridge",6));assert(write(accepted,"root",4)==4);enter(a);assert(read(client,bytes,sizeof(bytes))==4&&!memcmp(bytes,"root",4));
    enter(root);int native_client=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in local=endpoint("127.0.0.1",port);assert(connect(native_client,(void*)&local,sizeof(local))==0);int native_accepted=accept(root_server,NULL,NULL);assert(native_accepted>=0);assert(write(native_client,"native",6)==6);assert(read(native_accepted,bytes,sizeof(bytes))==6&&!memcmp(bytes,"native",6));
    enter(b);unsigned bi=index_of("eth0");up(bi);address(bi,"172.27.0.2",AF_INET,24);address(bi,"fd27::2",AF_INET6,64);default_route(bi,"172.27.0.1",AF_INET);default_route(bi,"fd27::1",AF_INET6);
    int receiver=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);struct sockaddr_in dest=endpoint("172.27.0.2",18402);any=endpoint("0.0.0.0",18402);assert(bind(receiver,(void*)&any,sizeof(any))==0);
    enter(a);int sender=socket(AF_INET,SOCK_DGRAM,0);assert(sendto(sender,"off",3,0,(void*)&dest,sizeof(dest))==3);enter(b);assert(recv(receiver,bytes,sizeof(bytes),0)==-1&&errno==EAGAIN);
    // Even after setns, the old sysctl FD still controls root, not the caller.
    enter(a);toggle(forward4,1);text_file("/proc/sys/net/ipv4/ip_forward","0\n");assert(sendto(sender,"routed",6,0,(void*)&dest,sizeof(dest))==6);enter(b);assert(recv(receiver,bytes,sizeof(bytes),0)==6&&!memcmp(bytes,"routed",6));
    enter(a);int ttl=1;assert(setsockopt(sender,IPPROTO_IP,IP_TTL,&ttl,sizeof(ttl))==0);assert(sendto(sender,"ttl1",4,0,(void*)&dest,sizeof(dest))==4);enter(b);assert(recv(receiver,bytes,sizeof(bytes),0)==-1&&errno==EAGAIN);enter(a);ttl=2;assert(setsockopt(sender,IPPROTO_IP,IP_TTL,&ttl,sizeof(ttl))==0);assert(sendto(sender,"ttl2",4,0,(void*)&dest,sizeof(dest))==4);enter(b);assert(recv(receiver,bytes,sizeof(bytes),0)==4&&!memcmp(bytes,"ttl2",4));
    int receiver6=socket(AF_INET6,SOCK_DGRAM|SOCK_NONBLOCK,0);struct sockaddr_in6 dest6=endpoint6("fd27::2",18403),any6=endpoint6("::",18403);assert(bind(receiver6,(void*)&any6,sizeof(any6))==0);enter(a);int sender6=socket(AF_INET6,SOCK_DGRAM,0);assert(sendto(sender6,"off6",4,0,(void*)&dest6,sizeof(dest6))==4);enter(b);assert(recv(receiver6,bytes,sizeof(bytes),0)==-1&&errno==EAGAIN);
    toggle(forward6,1);enter(a);assert(sendto(sender6,"route6",6,0,(void*)&dest6,sizeof(dest6))==6);enter(b);assert(recv(receiver6,bytes,sizeof(bytes),0)==6&&!memcmp(bytes,"route6",6));toggle(forward4,0);enter(a);assert(sendto(sender,"off again",9,0,(void*)&dest,sizeof(dest))==9);enter(b);assert(recv(receiver,bytes,sizeof(bytes),0)==-1&&errno==EAGAIN);
    enter(root);int host=socket(AF_INET,SOCK_DGRAM,0);assert(sendto(host,"root to b",9,0,(void*)&dest,sizeof(dest))==9);enter(b);struct sockaddr_in from;size=sizeof(from);assert(recvfrom(receiver,bytes,sizeof(bytes),0,(void*)&from,&size)==9);assert(from.sin_addr.s_addr==inet_addr("172.27.0.1"));assert(sendto(receiver,"reply",5,0,(void*)&from,size)==5);enter(root);assert(recv(host,bytes,sizeof(bytes),0)==5&&!memcmp(bytes,"reply",5));
    int handles[]={root,a,b,forward4,forward6,root_server,netlink,client,accepted,native_client,native_accepted,receiver,sender,receiver6,sender6,host};for(unsigned i=0;i<sizeof(handles)/sizeof(handles[0]);i++)close(handles[i]);
    puts("container forwarding: root native/bridge TCP/UDP, IPv4/IPv6 L3 policy, pinned sysctls and TTL: ok");return 0;
}
