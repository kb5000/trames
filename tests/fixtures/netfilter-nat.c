// SPDX-License-Identifier: GPL-3.0-or-later
#define main old_link_case
#include "container-network.c"
#undef main
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
static void cmd(const char *s){pid_t c=fork();assert(c>=0);if(!c){execl("/bin/sh","sh","-ec",s,(char*)NULL);_exit(127);}int status;assert(waitpid(c,&status,0)==c);if(!WIFEXITED(status)||WEXITSTATUS(status))fprintf(stderr,"failed: %s\n",s);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);}
static void route(unsigned dev){struct packet p;begin(&p,RTM_NEWROUTE,NLM_F_CREATE|NLM_F_EXCL,sizeof(struct rtmsg));struct rtmsg *r=(void*)p.data;r->rtm_family=AF_INET;r->rtm_table=RT_TABLE_MAIN;r->rtm_protocol=RTPROT_BOOT;r->rtm_type=RTN_UNICAST;number(&p,RTA_OIF,dev);struct in_addr gw;assert(inet_pton(AF_INET,"172.28.0.1",&gw)==1);attribute(&p,RTA_GATEWAY,&gw,4);int n=control();assert(send_packet(n,&p)==0);close(n);}
static int receive(int s,char *b,size_t n,struct sockaddr_in *from){for(int i=0;i<3000;i++){socklen_t size=sizeof(*from);int r=recvfrom(s,b,n,MSG_DONTWAIT,(void*)from,&size);if(r>=0)return r;assert(errno==EAGAIN);usleep(1000);}return -1;}
int main(int argc,char **argv){
    assert(argc==3);int tcp_port=atoi(argv[1]),udp_port=atoi(argv[2]);assert(tcp_port>0&&udp_port>0);
    int root=open("/proc/self/ns/net",O_RDONLY),a=space();enter(root);int b=space();enter(root);assert(root>=0);
    int nl=control();bridge(nl,"nat-br");unsigned br=index_of("nat-br");up(br);address(br,"172.28.0.1",AF_INET,24);veth(nl,"nat-a",a);veth(nl,"nat-b",b);master(nl,index_of("nat-a"),br);master(nl,index_of("nat-b"),br);up(index_of("nat-a"));up(index_of("nat-b"));
    int forward=open("/proc/sys/net/ipv4/ip_forward",O_WRONLY);assert(forward>=0&&write(forward,"1\n",2)==2);close(forward);
    enter(a);unsigned ai=index_of("eth0");up(ai);up(1);address(ai,"172.28.0.2",AF_INET,24);route(ai);int sender=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);assert(sender>=0);
    enter(b);unsigned bi=index_of("eth0");up(bi);up(1);address(bi,"172.28.0.3",AF_INET,24);route(bi);int receiver=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);struct sockaddr_in bindaddr=endpoint("0.0.0.0",19011);assert(bind(receiver,(void*)&bindaddr,sizeof(bindaddr))==0);
    enter(root);cmd("iptables -P FORWARD DROP; iptables -A FORWARD -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT; iptables -A FORWARD -i nat-br -j ACCEPT; iptables -t nat -A POSTROUTING -s 172.28.0.0/24 ! -o nat-br -j MASQUERADE");
    enter(a);struct sockaddr_in external=endpoint("198.18.0.1",udp_port),from;char bytes[2048];assert(sendto(sender,"egress",6,0,(void*)&external,sizeof(external))==6);int n=receive(sender,bytes,sizeof(bytes),&from);assert(n==6&&!memcmp(bytes,"egress",6));assert(from.sin_addr.s_addr==external.sin_addr.s_addr&&from.sin_port==external.sin_port);puts("NAT_UDP_PASS");fflush(stdout);
    int tcp=socket(AF_INET,SOCK_STREAM,0);external.sin_port=htons(tcp_port);assert(tcp>=0&&connect(tcp,(void*)&external,sizeof(external))==0);assert(write(tcp,"GET / HTTP/1.0\r\n\r\n",18)==18);n=read(tcp,bytes,sizeof(bytes)-1);assert(n>0);bytes[n]=0;assert(strstr(bytes,"200 OK"));close(tcp);puts("NAT_TCP_PASS");fflush(stdout);
    enter(root);cmd("iptables -t nat -A PREROUTING -d 172.28.0.1 -p udp --dport 19012 -j DNAT --to-destination 172.28.0.3:19011; iptables -t nat -I POSTROUTING -s 172.28.0.0/24 -d 172.28.0.3 -p udp --dport 19011 -j MASQUERADE");
    enter(a);struct sockaddr_in published=endpoint("172.28.0.1",19012);assert(sendto(sender,"hairpin",7,0,(void*)&published,sizeof(published))==7);
    enter(b);n=receive(receiver,bytes,sizeof(bytes),&from);assert(n==7&&!memcmp(bytes,"hairpin",7));assert(from.sin_addr.s_addr==inet_addr("172.28.0.1"));assert(sendto(receiver,bytes,n,0,(void*)&from,sizeof(from))==n);
    enter(a);n=receive(sender,bytes,sizeof(bytes),&from);assert(n==7&&!memcmp(bytes,"hairpin",7));assert(from.sin_addr.s_addr==published.sin_addr.s_addr&&from.sin_port==published.sin_port);puts("NAT_HAIRPIN_PASS");fflush(stdout);
    enter(root);cmd("iptables -I FORWARD 1 -i nat-br -o nat-br -j DROP");enter(a);struct sockaddr_in direct=endpoint("172.28.0.3",19011);assert(sendto(sender,"blocked",7,0,(void*)&direct,sizeof(direct))==7);enter(b);assert(recv(receiver,bytes,sizeof(bytes),0)==-1&&errno==EAGAIN);
    enter(root);cmd("iptables -D FORWARD -i nat-br -o nat-br -j DROP");enter(a);assert(sendto(sender,"samebridge",10,0,(void*)&direct,sizeof(direct))==10);enter(b);n=receive(receiver,bytes,sizeof(bytes),&from);assert(n==10&&!memcmp(bytes,"samebridge",10));puts("NAT_BRIDGE_FILTER_PASS");
    enter(a);cmd("iptables -t nat -A OUTPUT -d 127.0.0.11 -p udp --dport 53 -j DNAT --to-destination 127.0.0.11:19013");
    int resolver=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0),query=socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK,0);assert(resolver>=0&&query>=0);struct sockaddr_in resolver_bind=endpoint("127.0.0.11",19013),dns=endpoint("127.0.0.11",53);assert(!bind(resolver,(void*)&resolver_bind,sizeof(resolver_bind)));assert(sendto(query,"dns",3,0,(void*)&dns,sizeof dns)==3);n=receive(resolver,bytes,sizeof bytes,&from);assert(n==3);assert(sendto(resolver,bytes,n,0,(void*)&from,sizeof from)==n);n=receive(query,bytes,sizeof bytes,&from);assert(n==3&&from.sin_port==htons(53)&&from.sin_addr.s_addr==dns.sin_addr.s_addr);puts("NAT_LOOPBACK_PASS");
    puts("netfilter NAT: original iptables, TCP/UDP egress, reversible hairpin and same-bridge filtering: ok");return 0;
}
