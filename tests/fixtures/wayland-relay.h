// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only transparent Wayland byte/SCM_RIGHTS relay, like Firefox's proxy.
#include <assert.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int relay_packet(int source,int destination) {
    char bytes[4096];union {struct cmsghdr align;char bytes[CMSG_SPACE(28*sizeof(int))];} control;
    struct iovec iov={bytes,sizeof(bytes)};
    struct msghdr msg={.msg_iov=&iov,.msg_iovlen=1,.msg_control=control.bytes,.msg_controllen=sizeof(control.bytes)};
    ssize_t count;
    do count=recvmsg(source,&msg,0);while(count<0 && errno==EINTR);
    if(count<=0)return 0;
    assert(!(msg.msg_flags&(MSG_TRUNC|MSG_CTRUNC)));
    iov.iov_len=(size_t)count;size_t sent=0;
    while(sent<(size_t)count){
        ssize_t n=sendmsg(destination,&msg,MSG_NOSIGNAL);
        if(n<0 && errno==EINTR)continue;
        // Close received rights after their first successful transfer (or error).
        for(struct cmsghdr *c=CMSG_FIRSTHDR(&msg);c;c=CMSG_NXTHDR(&msg,c))
            if(c->cmsg_level==SOL_SOCKET && c->cmsg_type==SCM_RIGHTS)
                for(size_t i=0;i<(c->cmsg_len-CMSG_LEN(0))/sizeof(int);i++)close(((int *)CMSG_DATA(c))[i]);
        msg.msg_control=NULL;msg.msg_controllen=0;
        if(n<=0)return 0;
        sent+=(size_t)n;iov.iov_base=bytes+sent;iov.iov_len=(size_t)count-sent;
    }
    return 1;
}
static void *relay_wayland(void *data) {
    int *sockets=data;struct pollfd fds[2]={{sockets[0],POLLIN,0},{sockets[1],POLLIN,0}};free(sockets);
    for(;;){
        int result=poll(fds,2,-1);if(result<0 && errno==EINTR)continue;if(result<0)break;
        int alive=1;
        for(int i=0;i<2;i++)if(fds[i].revents && !relay_packet(fds[i].fd,fds[1-i].fd)){alive=0;break;}
        if(!alive)break;
    }
    close(fds[0].fd);close(fds[1].fd);return NULL;
}
static struct wl_display *test_wayland_connect(void) {
    if(!getenv("TRAMES_TEST_WAYLAND_RELAY"))return wl_display_connect(NULL);
    const char *name=getenv("WAYLAND_DISPLAY"),*runtime=getenv("XDG_RUNTIME_DIR");
    if(!name)name="wayland-0";
    if(!runtime)runtime="/run/user/0";
    struct sockaddr_un address={.sun_family=AF_UNIX};
    int length=snprintf(address.sun_path,sizeof(address.sun_path),"%s/%s",runtime,name);
    assert(length>0 && (size_t)length<sizeof(address.sun_path));
    int native=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0),pair[2];assert(native>=0);
    assert(connect(native,(struct sockaddr *)&address,sizeof(address))==0);
    assert(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair)==0);
    int *sockets=malloc(2*sizeof(int));assert(sockets);sockets[0]=native;sockets[1]=pair[1];
    pthread_t worker;assert(pthread_create(&worker,NULL,relay_wayland,sockets)==0);assert(pthread_detach(worker)==0);
    return wl_display_connect_to_fd(pair[0]);
}
