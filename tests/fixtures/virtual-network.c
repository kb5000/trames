// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"virtual network line %d errno %d\n",__LINE__,errno);exit(2); } } while(0)
int main(void) {
    int fd=socket(AF_INET,SOCK_DGRAM,0);CHECK(fd>=0);
    struct ifreq req={0};strcpy(req.ifr_name,"trames0");CHECK(ioctl(fd,SIOCGIFADDR,&req)==0);
    char ip[64];CHECK(inet_ntop(AF_INET,&((struct sockaddr_in*)&req.ifr_addr)->sin_addr,ip,sizeof ip));CHECK(!strcmp(ip,"10.0.0.2"));
    CHECK(ioctl(fd,SIOCGIFMTU,&req)==0 && req.ifr_mtu==1500);CHECK(ioctl(fd,SIOCGIFINDEX,&req)==0 && req.ifr_ifindex==2);
    struct ifreq entries[2];struct ifconf interfaces={.ifc_len=sizeof entries,.ifc_req=entries};CHECK(ioctl(fd,SIOCGIFCONF,&interfaces)==0 && interfaces.ifc_len==sizeof entries);CHECK(!strcmp(entries[0].ifr_name,"lo") && !strcmp(entries[1].ifr_name,"trames0"));
    close(fd);
    struct addrinfo hints={.ai_family=AF_UNSPEC,.ai_socktype=SOCK_STREAM},*answers=NULL;CHECK(getaddrinfo("env2.trames","80",&hints,&answers)==0);int v4=0,v6=0;
    for (struct addrinfo *a=answers;a;a=a->ai_next) {if(a->ai_family==AF_INET){CHECK(inet_ntop(AF_INET,&((struct sockaddr_in*)a->ai_addr)->sin_addr,ip,sizeof ip));CHECK(!strcmp(ip,"10.0.0.2"));v4++;}else if(a->ai_family==AF_INET6){CHECK(inet_ntop(AF_INET6,&((struct sockaddr_in6*)a->ai_addr)->sin6_addr,ip,sizeof ip));CHECK(!strcmp(ip,"fd00::2"));v6++;}}
    freeaddrinfo(answers);CHECK(v4==1 && v6==1);
    CHECK(getaddrinfo("missing.invalid.trames","80",&hints,&answers)!=0);
    for (int ipv6=0;ipv6<=1;ipv6++) {
        int family=ipv6?AF_INET6:AF_INET;fd=socket(family,SOCK_DGRAM,ipv6?IPPROTO_ICMPV6:IPPROTO_ICMP);CHECK(fd>=0);
        unsigned char message[]={0,0,0,0,0,0,0,7,'e','c','h','o'};message[0]=ipv6?128:8;
        struct sockaddr_storage target={0};socklen_t size;
        if(ipv6){struct sockaddr_in6 *a=(void*)&target;a->sin6_family=family;CHECK(inet_pton(family,"fd00::1",&a->sin6_addr)==1);size=sizeof *a;}
        else{struct sockaddr_in *a=(void*)&target;a->sin_family=family;CHECK(inet_pton(family,"10.0.0.1",&a->sin_addr)==1);size=sizeof *a;}
        CHECK(sendto(fd,message,sizeof message,0,(void*)&target,size)==sizeof message);
        unsigned char reply[64];CHECK(recv(fd,reply,sizeof reply,0)==sizeof message);CHECK(reply[0]==(ipv6?129:0) && !memcmp(reply+8,message+8,4));close(fd);
    }
    puts("virtual interfaces, musl DNS A/AAAA and ICMP echo: ok");return 0;
}
