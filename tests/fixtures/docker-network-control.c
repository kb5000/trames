// SPDX-License-Identifier: GPL-3.0-or-later
#define main existing_container_network_case
#include "container-network.c"
#undef main

// The wire formats used by unmodified Go netlink and BusyBox differ from
// the older hand-built fixture: no NUL in INFO_KIND, queue attrs and rtgenmsg.
static void docker_bridge(int fd) {
    struct packet p;
    begin(&p, RTM_NEWLINK, NLM_F_CREATE|NLM_F_EXCL, sizeof(struct ifinfomsg));
    attribute(&p, IFLA_IFNAME, "docker0", 8);
    number(&p, IFLA_TXQLEN, 0);
    unsigned char mac[6]={2,0,0,0,0,42}; attribute(&p, IFLA_ADDRESS, mac, 6);
    size_t info=nest(&p, IFLA_LINKINFO); attribute(&p, IFLA_INFO_KIND, "bridge", 6);
    size_t data=nest(&p, IFLA_INFO_DATA); end_nest(&p,data); end_nest(&p,info);
    assert(send_packet(fd,&p)==0);
}
static void compact_dump(int fd, unsigned kind, unsigned family, size_t bytes) {
    struct packet p; begin(&p, kind, NLM_F_DUMP, bytes); p.data[0]=family;
    struct sockaddr_nl to={.nl_family=AF_NETLINK};
    assert(sendto(fd,&p,p.h.nlmsg_len,0,(void*)&to,sizeof(to))==(ssize_t)p.h.nlmsg_len);
    int done=0;
    for (unsigned count=0; !done && count<128; count++) {
        char reply[32768]; ssize_t size=recv(fd,reply,sizeof(reply),0); assert(size>0);
        for (struct nlmsghdr *h=(void*)reply; NLMSG_OK(h,size); h=NLMSG_NEXT(h,size)) {
            assert(h->nlmsg_type!=NLMSG_ERROR); done |= h->nlmsg_type==NLMSG_DONE;
        }
    }
    assert(done);
}
static void subscriber_dump(void) {
    int fd=control(); struct sockaddr_nl group={.nl_family=AF_NETLINK,.nl_groups=1};
    assert(bind(fd,(void*)&group,sizeof(group))==0);
    struct packet p;begin(&p,RTM_GETLINK,NLM_F_DUMP,sizeof(struct ifinfomsg));
    assert(sendto(fd,&p,p.h.nlmsg_len,0,(void*)&group,sizeof(group))==(ssize_t)p.h.nlmsg_len);
    int done=0;
    while(!done) {
        char bytes[32768]; struct sockaddr_nl from; socklen_t len=sizeof(from);
        ssize_t count=recvfrom(fd,bytes,sizeof(bytes),0,(void*)&from,&len);assert(count>0);
        assert(from.nl_family==AF_NETLINK && from.nl_pid==0);
        struct nlmsghdr *h=(void*)bytes;assert(h->nlmsg_type!=NLMSG_ERROR);done=h->nlmsg_type==NLMSG_DONE;
    }
    close(fd);
}
static void route_get(int fd, unsigned index) {
    struct packet p;begin(&p,RTM_GETROUTE,0,sizeof(struct rtmsg));p.h.nlmsg_flags &= ~NLM_F_ACK;
    struct rtmsg *rt=(void*)p.data;rt->rtm_family=AF_INET;rt->rtm_dst_len=32;rt->rtm_flags=RTM_F_LOOKUP_TABLE;
    struct in_addr dst;assert(inet_pton(AF_INET,"192.0.2.2",&dst)==1);attribute(&p,RTA_DST,&dst,4);
    struct sockaddr_nl to={.nl_family=AF_NETLINK};assert(sendto(fd,&p,p.h.nlmsg_len,0,(void*)&to,sizeof(to))==(ssize_t)p.h.nlmsg_len);
    char reply[4096];ssize_t n=recv(fd,reply,sizeof(reply),0);assert(n>0);
    struct nlmsghdr *h=(void*)reply;assert(h->nlmsg_type==RTM_NEWROUTE);
    rt=NLMSG_DATA(h);int len=RTM_PAYLOAD(h),found=0;
    for(struct rtattr *a=RTM_RTA(rt);RTA_OK(a,len);a=RTA_NEXT(a,len)) {
        if(a->rta_type==RTA_OIF){unsigned value;memcpy(&value,RTA_DATA(a),4);assert(value==index);found=1;}
    }
    assert(found);
}
int main(void) {
    int fd=control(); docker_bridge(fd); unsigned br=index_of("docker0");
    text_file("/sys/class/net/docker0/tx_queue_len","0\n");
    struct packet p; begin(&p, RTM_SETLINK, 0, sizeof(struct ifinfomsg));
    ((struct ifinfomsg*)p.data)->ifi_index=br; number(&p,IFLA_TXQLEN,23);
    assert(send_packet(fd,&p)==0); text_file("/sys/class/net/docker0/tx_queue_len","23\n");
    // A rejected rename must also roll back the queue change.
    number(&p,IFLA_IFNAME,0); assert(send_packet(fd,&p)==-EINVAL);
    text_file("/sys/class/net/docker0/tx_queue_len","23\n");
    begin(&p,RTM_SETLINK,0,sizeof(struct ifinfomsg)); ((struct ifinfomsg*)p.data)->ifi_index=br;
    attribute(&p,IFLA_IFNAME,"docker-renamed",14); assert(send_packet(fd,&p)==0);
    text_file("/sys/class/net/docker-renamed/tx_queue_len","23\n");
    up(br);address(br,"192.0.2.1",AF_INET,24);route_get(fd,br);
    for(unsigned family=0; family<=AF_INET6; family+=(family?AF_INET6-AF_INET:AF_INET)) {
        for(size_t len=1;len<=4;len+=3) {
            compact_dump(fd,RTM_GETLINK,family,len);
            compact_dump(fd,RTM_GETADDR,family,len);
            compact_dump(fd,RTM_GETROUTE,family,len);
        }
    }
    compact_dump(fd,RTM_GETLINK,AF_PACKET,4);
    subscriber_dump();
    int root=open("/proc/self/ns/net",O_RDONLY);assert(root>=0);int child=space();enter(root);
    veth(fd,"docker-peer",child);unsigned peer=index_of("docker-peer");master(fd,peer,br);up(peer);
    text_file("/sys/class/net/docker-renamed/brif/docker-peer/state","0\n");
    enter(child);up(index_of("eth0"));enter(root);
    text_file("/sys/class/net/docker-renamed/brif/docker-peer/state","3\n");
    begin(&p,RTM_SETLINK,0,sizeof(struct ifinfomsg));((struct ifinfomsg*)p.data)->ifi_index=br;
    ((struct ifinfomsg*)p.data)->ifi_change=IFF_UP;assert(send_packet(fd,&p)==0);
    text_file("/sys/class/net/docker-renamed/brif/docker-peer/state","0\n");up(br);
    master(fd,peer,0);
    assert(open("/sys/class/net/docker-renamed/brif/docker-peer/state",O_RDONLY)<0 && errno==ENOENT);
    close(child);close(root);
    close(fd); puts("docker network control: Go link/queue, BusyBox dumps, subscriptions and RouteGet: ok");
    return 0;
}
