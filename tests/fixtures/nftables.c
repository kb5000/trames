// SPDX-License-Identifier: GPL-3.0-or-later
// Original Alpine iptables-nft programs configure a real routed guest topology.
#define main previous_forwarding_case
#include "container-network.c"
#undef main
#include <stdlib.h>
#include <sys/wait.h>
static void default_route(unsigned index,const char *gateway,int family) {
    struct packet p;begin(&p,RTM_NEWROUTE,NLM_F_CREATE|NLM_F_EXCL,sizeof(struct rtmsg));
    struct rtmsg *r=(void*)p.data;r->rtm_family=family;r->rtm_table=RT_TABLE_MAIN;r->rtm_protocol=RTPROT_BOOT;r->rtm_type=RTN_UNICAST;
    number(&p,RTA_OIF,index);unsigned char ip[16];assert(inet_pton(family,gateway,ip)==1);attribute(&p,RTA_GATEWAY,ip,family==AF_INET?4:16);int fd=control();assert(send_packet(fd,&p)==0);close(fd);
}
static void toggle(int fd,int enabled) {assert(lseek(fd,0,SEEK_SET)==0);assert(write(fd,enabled?"1\n":"0\n",2)==2);}
static struct sockaddr_in6 endpoint6(const char *ip,int port) {struct sockaddr_in6 a={.sin6_family=AF_INET6,.sin6_port=htons(port)};assert(inet_pton(AF_INET6,ip,&a.sin6_addr)==1);return a;}

static int tool(const char *command) {
    pid_t child=fork();assert(child>=0);
    if(!child){execl("/bin/sh","sh","-ec",command,(char*)NULL);_exit(127);}
    int status;assert(waitpid(child,&status,0)==child);return WIFEXITED(status)?WEXITSTATUS(status):128;
}
static void command(const char *line){int result=tool(line);if(result)fprintf(stderr,"tool failed (%d): %s\n",result,line);assert(result==0);}
static void send4(int s,int r,const char *ip,int port,int blocked) {
    struct sockaddr_in a=endpoint(ip,port);assert(sendto(s,"packet",6,0,(void*)&a,sizeof(a))==6);
    char bytes[64];ssize_t n=recv(r,bytes,sizeof(bytes),0);assert(blocked?(n==-1&&errno==EAGAIN):(n==6&&!memcmp(bytes,"packet",6)));
}
static void send6(int s,int r,const char *ip,int port,int blocked) {
    struct sockaddr_in6 a=endpoint6(ip,port);assert(sendto(s,"packet6",7,0,(void*)&a,sizeof(a))==7);
    char bytes[64];ssize_t n=recv(r,bytes,sizeof(bytes),0);assert(blocked?(n==-1&&errno==EAGAIN):(n==7&&!memcmp(bytes,"packet6",7)));
}
int main(void) {
    int native=open("/proc/self/ns/net",O_RDONLY);assert(native>=0);
    int router=space();int a=space();enter(router);int b=space();enter(router);
    int nl=control();bridge(nl,"nft-a");bridge(nl,"nft-b");unsigned ba=index_of("nft-a"),bb=index_of("nft-b");up(ba);up(bb);
    address(ba,"10.11.0.1",AF_INET,24);address(bb,"10.12.0.1",AF_INET,24);address(ba,"fd11::1",AF_INET6,64);address(bb,"fd12::1",AF_INET6,64);
    veth(nl,"nft-pa",a);veth(nl,"nft-pb",b);unsigned pa=index_of("nft-pa"),pb=index_of("nft-pb");master(nl,pa,ba);master(nl,pb,bb);up(pa);up(pb);
    int f4=open("/proc/sys/net/ipv4/ip_forward",O_RDWR),f6=open("/proc/sys/net/ipv6/conf/all/forwarding",O_RDWR);assert(f4>=0&&f6>=0);toggle(f4,1);toggle(f6,1);
    enter(a);unsigned ai=index_of("eth0");up(ai);up(1);address(ai,"10.11.0.2",AF_INET,24);address(ai,"fd11::2",AF_INET6,64);default_route(ai,"10.11.0.1",AF_INET);default_route(ai,"fd11::1",AF_INET6);
    int s4=socket(AF_INET,SOCK_DGRAM,0),s6=socket(AF_INET6,SOCK_DGRAM,0);assert(s4>=0&&s6>=0);
    enter(b);unsigned bi=index_of("eth0");up(bi);up(1);address(bi,"10.12.0.2",AF_INET,24);address(bi,"fd12::2",AF_INET6,64);default_route(bi,"10.12.0.1",AF_INET);default_route(bi,"fd12::1",AF_INET6);
    int r4=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0),r6=socket(AF_INET6,SOCK_DGRAM|SOCK_NONBLOCK,0),other=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);assert(r4>=0&&r6>=0&&other>=0);
    struct sockaddr_in any=endpoint("0.0.0.0",19001);assert(bind(r4,(void*)&any,sizeof(any))==0);any.sin_port=htons(19002);assert(bind(other,(void*)&any,sizeof(any))==0);
    int only6=1;assert(setsockopt(r6,IPPROTO_IPV6,IPV6_V6ONLY,&only6,sizeof(only6))==0);
    struct sockaddr_in6 any6=endpoint6("::",19001);assert(bind(r6,(void*)&any6,sizeof(any6))==0);
    enter(a);send4(s4,r4,"10.12.0.2",19001,0);send6(s6,r6,"fd12::2",19001,0);
    enter(router);command("iptables --version; iptables -N TRAMES; iptables -A TRAMES -s 10.11.0.0/24 -o nft-b -p udp --dport 19001 -j DROP; iptables -A FORWARD -j TRAMES; iptables -C FORWARD -j TRAMES");
    command("ip6tables -N TRAMES; ip6tables -A TRAMES -s fd11::/64 -p udp --dport 19001 -j DROP; ip6tables -A FORWARD -j TRAMES");
    enter(a);send4(s4,r4,"10.12.0.2",19001,1);send6(s6,r6,"fd12::2",19001,1);send4(s4,other,"10.12.0.2",19002,0);
    enter(router);command("iptables -S; iptables -v -n -L TRAMES; ip6tables -v -n -L TRAMES; test $(iptables -v -n -L TRAMES | awk '$3==\"DROP\"{print $1}') = 1; test $(ip6tables -v -n -L TRAMES | awk '$3==\"DROP\"{print $1}') = 1");
    // Unsupported ct/NAT cannot leave a partially-installed rule behind.
    assert(tool("iptables -A TRAMES -m conntrack --ctstate ESTABLISHED -j ACCEPT")!=0);
    enter(a);send4(s4,r4,"10.12.0.2",19001,1);
    enter(router);command("iptables -I TRAMES 1 -p udp --dport 19001 -j ACCEPT; iptables -C TRAMES -p udp --dport 19001 -j ACCEPT");
    enter(a);send4(s4,r4,"10.12.0.2",19001,0);
    enter(router);command("iptables -R TRAMES 1 -p udp --dport 19001 -j DROP");enter(a);send4(s4,r4,"10.12.0.2",19001,1);
    enter(router);command("iptables -D FORWARD -j TRAMES; iptables -F TRAMES; iptables -X TRAMES; iptables -P FORWARD DROP");enter(a);send4(s4,r4,"10.12.0.2",19001,1);
    enter(router);command("iptables -P FORWARD ACCEPT; ip6tables -F; ip6tables -X TRAMES");enter(a);send4(s4,r4,"10.12.0.2",19001,0);send6(s6,r6,"fd12::2",19001,0);
    // Namespace B does not inherit the router's rules. Its INPUT policy really drops.
    enter(b);command("iptables -P INPUT DROP");enter(a);send4(s4,r4,"10.12.0.2",19001,1);
    enter(b);command("iptables -P INPUT ACCEPT");enter(a);send4(s4,r4,"10.12.0.2",19001,0);
    enter(native);assert(tool("iptables -t nat -A POSTROUTING -j MASQUERADE")!=0);
    int handles[]={native,router,a,b,nl,f4,f6,s4,s6,r4,r6,other};for(unsigned i=0;i<sizeof(handles)/sizeof(handles[0]);i++)close(handles[i]);
    puts("nftables: original iptables-nft/ip6tables, IPv4/IPv6 DROP/ACCEPT, counters, rule lifecycle and namespace policy: ok");return 0;
}
