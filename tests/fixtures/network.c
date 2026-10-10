// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "network line %d errno %d\n", __LINE__, errno); exit(2); } } while (0)
static struct sockaddr_storage peer;
static socklen_t peer_size;
static int family;
static volatile sig_atomic_t signals;
static void handler(int sig) { signals += sig == SIGUSR1; }
static void wait_ready(int fd, short events) {
    struct pollfd p = { .fd = fd, .events = events };
    CHECK(poll(&p, 1, 5000) == 1 && p.revents & (events | POLLERR | POLLHUP));
}
static void setup(int ipv6, unsigned port) {
    memset(&peer, 0, sizeof peer);
    family = ipv6 ? AF_INET6 : AF_INET;
    if (ipv6) {
        struct sockaddr_in6 *v = (void *)&peer;
        v->sin6_family = AF_INET6; v->sin6_port = htons(port);
        CHECK(inet_pton(AF_INET6, "::1", &v->sin6_addr) == 1);
        peer_size = sizeof *v;
    } else {
        struct sockaddr_in *v = (void *)&peer;
        v->sin_family = AF_INET; v->sin_port = htons(port);
        CHECK(inet_pton(AF_INET, "127.0.0.1", &v->sin_addr) == 1);
        peer_size = sizeof *v;
    }
}
static int connected(int kind, int nonblocking) {
    int fd = socket(family, kind | SOCK_CLOEXEC | (nonblocking ? SOCK_NONBLOCK : 0), 0);
    CHECK(fd >= 0);
    int result = connect(fd, (void *)&peer, peer_size);
    if (result < 0) {
        CHECK(nonblocking && errno == EINPROGRESS);
        wait_ready(fd, POLLOUT);
        int error = -1; socklen_t size = sizeof error;
        CHECK(getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0 && size == sizeof error);
    }
    struct sockaddr_storage local; socklen_t size = sizeof local;
    CHECK(getsockname(fd, (void *)&local, &size) == 0 && local.ss_family == family && size == peer_size);
    size = sizeof local;
    CHECK(getpeername(fd, (void *)&local, &size) == 0 && size == peer_size);
    CHECK(memcmp(&local, &peer, peer_size) == 0);
    return fd;
}
static void udp(int preflight) {
    int fd = connected(SOCK_DGRAM, 1);
    int duplicate = dup(fd); CHECK(duplicate >= 0);
    CHECK(fcntl(duplicate, F_GETFD) == 0 && fcntl(fd, F_GETFD) == FD_CLOEXEC);
    int enabled = 0;
    CHECK(ioctl(duplicate, FIONBIO, &enabled) == 0 && !(fcntl(fd,F_GETFL) & O_NONBLOCK));
    enabled = 1;
    CHECK(ioctl(fd, FIONBIO, &enabled) == 0 && fcntl(duplicate,F_GETFL) & O_NONBLOCK);
    CHECK(close(fd) == 0); fd = duplicate;
    int type = 0; socklen_t size = sizeof type;
    CHECK(getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &size) == 0 && type == SOCK_DGRAM);
    char data[16] = {0};
    CHECK(recv(fd, data, sizeof data, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    const char first[] = {'a',0,'b'}, second[] = {':','u','d','p'};
    struct iovec outgoing[] = {{(void *)first,sizeof first},{(void *)second,sizeof second}};
    struct msghdr msg = {.msg_iov=outgoing, .msg_iovlen=2};
    CHECK(sendmsg(fd, &msg, MSG_NOSIGNAL) == 7);
    wait_ready(fd, POLLIN);
    struct iovec incoming[] = {{data,2},{(void *)(uintptr_t)0xdead00000000ULL,2}};
    msg = (struct msghdr){.msg_iov=incoming, .msg_iovlen=2};
    if (preflight) CHECK(recvmsg(fd, &msg, 0) == -1 && errno == EFAULT);
    incoming[1].iov_base = data+2;
    struct sockaddr_storage source;
    msg.msg_name = &source; msg.msg_namelen = sizeof source;
    CHECK(recvmsg(fd, &msg, MSG_PEEK) == 4 && msg.msg_flags & MSG_TRUNC);
    CHECK(msg.msg_namelen == peer_size && !memcmp(&source, &peer, peer_size));
    CHECK(!memcmp(data, "a\0b:", 4));
    size = sizeof source;
    CHECK(recvfrom(fd, data, 3, MSG_TRUNC, (void *)&source, &size) == 7);
    CHECK(!memcmp(data, "a\0b", 3) && size == peer_size);
    CHECK(recv(fd, data, 1, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    CHECK(send(fd, "", 0, MSG_NOSIGNAL) == 0);
    wait_ready(fd, POLLIN);
    CHECK(recv(fd, data, 0, 0) == 0);
    CHECK(recv(fd, data, 1, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    CHECK(fcntl(fd, F_SETFL, 0) == 0);
    CHECK(sendto(fd, "end", 3, MSG_NOSIGNAL, (void *)&peer, peer_size) == 3);
    CHECK(recv(fd, data, sizeof data, 0) == 3 && !memcmp(data, "end", 3));
    CHECK(close(fd) == 0); puts("udp-ok");
}
static unsigned char pattern[256*1024], response[256*1024];
static void tcp(void) {
    int fd = connected(SOCK_STREAM, 1), yes = 1, value = 0;
    socklen_t size = sizeof value;
    CHECK(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes) == 0);
    CHECK(getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &value, &size) == 0 && value == 1);
    CHECK(fcntl(fd, F_SETFL, 0) == 0);
    CHECK(recv(fd, response, 1, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    for (size_t i=0; i<sizeof pattern; i++) pattern[i] = (i*37+11) & 255;
    size_t position = 0;
    while (position < sizeof pattern) {
        ssize_t n = write(fd, pattern+position, sizeof pattern-position);
        CHECK(n > 0); position += n;
    }
    CHECK(shutdown(fd, SHUT_WR) == 0);
    position = 0;
    while (position < sizeof response) {
        ssize_t n = read(fd, response+position, sizeof response-position);
        CHECK(n > 0); position += n;
    }
    CHECK(!memcmp(pattern, response, sizeof pattern));
    CHECK(read(fd, response, 1) == 0);
    CHECK(close(fd) == 0); puts("tcp-ok");
}
static void refused(int nonblocking) {
    int fd = socket(family, SOCK_STREAM | (nonblocking ? SOCK_NONBLOCK : 0), 0); CHECK(fd >= 0);
    int r = connect(fd, (void *)&peer, peer_size);
    CHECK(r == -1 && (errno == EINPROGRESS || errno == ECONNREFUSED));
    if (errno == EINPROGRESS) {
        wait_ready(fd, POLLOUT);
        int error = 0; socklen_t size = sizeof error;
        CHECK(getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == ECONNREFUSED);
        error = -1;
        CHECK(getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0);
    }
    int error = -1; socklen_t size = sizeof error;
    CHECK(getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0);
    char data;
    if (nonblocking) CHECK(recv(fd, &data, 1, MSG_DONTWAIT) == 0);
    else CHECK(recv(fd, &data, 1, MSG_DONTWAIT) == -1 && errno == ENOTCONN);
    CHECK(send(fd, "x", 1, MSG_NOSIGNAL) == -1 && errno == EPIPE);
    CHECK(close(fd) == 0); puts("refused-ok");
}
static int listener(void) {
    int fd = socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0); CHECK(fd >= 0);
    CHECK(bind(fd, (void *)&peer, peer_size) == 0);
    peer_size = sizeof peer;
    CHECK(getsockname(fd, (void *)&peer, &peer_size) == 0 && listen(fd, 16) == 0);
    return fd;
}
static void fork_server(void) {
    int fd = listener();
    pid_t child = fork(); CHECK(child >= 0);
    if (!child) {
        CHECK(close(fd) == 0);
        int client = connected(SOCK_STREAM, 0);
        CHECK(write(client, "child", 5) == 5 && close(client) == 0); _exit(0);
    }
    int client = accept4(fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK); CHECK(client >= 0);
    CHECK(fcntl(client, F_GETFD) == FD_CLOEXEC && fcntl(client, F_GETFL) & O_NONBLOCK);
    CHECK(fcntl(client, F_SETFL, 0) == 0);
    char data[8]; CHECK(read(client, data, 5) == 5 && !memcmp(data, "child", 5));
    int status; CHECK(waitpid(child, &status, 0) == child && status == 0);
    CHECK(read(client, data, 1) == 0);
    CHECK(close(client) == 0 && close(fd) == 0); puts("fork-server-ok");
}
static void host_server(void) {
    int fd = listener();
    unsigned port = family == AF_INET ? ntohs(((struct sockaddr_in *)&peer)->sin_port) : ntohs(((struct sockaddr_in6 *)&peer)->sin6_port);
    printf("PORT %u\n", port); fflush(stdout);
    struct sockaddr_storage source; socklen_t size = sizeof source;
    int client = accept(fd, (void *)&source, &size); CHECK(client >= 0);
    CHECK(source.ss_family == family && size == peer_size);
    char data[8]; CHECK(read(client, data, sizeof data) == 4 && !memcmp(data, "host", 4));
    CHECK(write(client, data, 4) == 4);
    CHECK(close(client) == 0 && close(fd) == 0); puts("host-server-ok");
}
static void signal_wait(int restart) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0); CHECK(fd >= 0);
    CHECK(bind(fd, (void *)&peer, peer_size) == 0);
    socklen_t size = sizeof peer; CHECK(getsockname(fd, (void *)&peer, &size) == 0);
    struct sigaction action = {.sa_handler=handler, .sa_flags=restart ? SA_RESTART : 0};
    CHECK(sigaction(SIGUSR1, &action, NULL) == 0);
    pid_t child = fork(); CHECK(child >= 0);
    if (!child) {
        usleep(50000); CHECK(kill(getppid(), SIGUSR1) == 0);
        usleep(50000); CHECK(sendto(fd, "z", 1, MSG_NOSIGNAL, (void *)&peer, peer_size) == 1);
        _exit(0);
    }
    char data;
    if (!restart) CHECK(recv(fd, &data, 1, 0) == -1 && errno == EINTR);
    CHECK(recv(fd, &data, 1, 0) == 1 && data == 'z' && signals == 1);
    int status; CHECK(waitpid(child, &status, 0) == child && status == 0);
    CHECK(close(fd) == 0); puts(restart ? "restart-ok" : "eintr-ok");
}
static void sigpipe(void) {
    int fd = connected(SOCK_STREAM, 0);
    CHECK(shutdown(fd, SHUT_WR) == 0);
    CHECK(send(fd, "x", 1, MSG_NOSIGNAL) == -1 && errno == EPIPE);
    CHECK(write(fd, "x", 1) == -1);
    _exit(3);
}
static void cancel(int fault) {
    int fd = connected(SOCK_STREAM, 0);
    CHECK(fd >= 0);
    pid_t child = fork(); CHECK(child >= 0);
    if (!child) { sleep(1000); _exit(3); }
    if (fault) __asm__ volatile(".word 0");
    puts("cancel-ok");
}
static void dns(void) {
    struct addrinfo hints = {.ai_family=AF_UNSPEC, .ai_socktype=SOCK_STREAM}, *list = NULL;
    CHECK(getaddrinfo("trames.test", "443", &hints, &list) == 0);
    unsigned v4=0, v6=0;
    for (struct addrinfo *p=list; p; p=p->ai_next) {
        char text[INET6_ADDRSTRLEN];
        if (p->ai_family == AF_INET) {
            struct sockaddr_in *v = (void *)p->ai_addr;
            CHECK(inet_ntop(AF_INET, &v->sin_addr, text, sizeof text));
            CHECK(!strcmp(text,"127.0.0.42") && ntohs(v->sin_port)==443); v4++;
        } else if (p->ai_family == AF_INET6) {
            struct sockaddr_in6 *v = (void *)p->ai_addr;
            CHECK(inet_ntop(AF_INET6, &v->sin6_addr, text, sizeof text));
            CHECK(!strcmp(text,"::1") && ntohs(v->sin6_port)==443); v6++;
        } else CHECK(0);
    }
    freeaddrinfo(list); CHECK(v4==1 && v6==1); puts("dns-ok");
}
static void policy(const char *mode) {
    if (!strcmp(mode,"off")) {
        CHECK(socket(AF_INET,SOCK_STREAM,0)==-1 && errno==EACCES);
    } else if (!strcmp(mode,"loopback")) {
        int fd=socket(AF_INET,SOCK_DGRAM,0); CHECK(fd>=0);
        struct sockaddr_in v={.sin_family=AF_INET,.sin_port=htons(12345)};
        CHECK(inet_pton(AF_INET,"192.0.2.1",&v.sin_addr)==1);
        CHECK(sendto(fd,"x",1,0,(void *)&v,sizeof v)==-1 && errno==EACCES);
        CHECK(close(fd)==0);
    } else if (!strcmp(mode,"outbound")) {
        int fd=socket(AF_INET,SOCK_STREAM,0); CHECK(fd>=0);
        struct sockaddr_in v={.sin_family=AF_INET};
        CHECK(bind(fd,(void *)&v,sizeof v)==-1 && errno==EACCES);
        CHECK(listen(fd,1)==-1 && errno==EINVAL); CHECK(close(fd)==0);
    } else if (!strcmp(mode,"full")) {
        int fd=socket(AF_INET,SOCK_STREAM,0); CHECK(fd>=0);
        struct sockaddr_in v={.sin_family=AF_INET};
        CHECK(bind(fd,(void *)&v,sizeof v)==0 && listen(fd,1)==0); CHECK(close(fd)==0);
    } else CHECK(0);
    puts("policy-ok");
}
static void socket_abi(void) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0); CHECK(fd >= 0);
    char data[16] = {0};
    CHECK(send(fd,"x",1,MSG_NOSIGNAL)==-1 && errno==EDESTADDRREQ);
    socklen_t size = sizeof data;
    CHECK(getpeername(fd,(void *)data,&size)==-1 && errno==ENOTCONN);
    CHECK(bind(fd,(void *)&peer,1)==-1 && errno==EINVAL);
    CHECK(syscall(SYS_sendto,fd,"x",1,0,(void *)(uintptr_t)0xdead00000000ULL,16)==-1 && errno==EFAULT);
    int option = 1;
    CHECK(setsockopt(fd,SOL_SOCKET,0xffff,&option,sizeof option)==-1 && errno==ENOPROTOOPT);
    struct iovec vector = {data,1};
    struct msghdr message = {.msg_iov=&vector,.msg_iovlen=1025};
    CHECK(sendmsg(fd,&message,0)==-1 && errno==EMSGSIZE);
    size = 2; memset(data,0x55,sizeof data);
    CHECK(getsockname(fd,(void *)data,&size)==0 && size==16);
    CHECK(data[0]==AF_INET && data[1]==0 && data[2]==0x55);
    size = 2; option = 0;
    CHECK(getsockopt(fd,SOL_SOCKET,SO_TYPE,&option,&size)==0 && size==2 && option==SOCK_DGRAM);
    CHECK(close(fd)==0);
    puts("socket-abi-ok");
}
int main(int argc, char **argv) {
    CHECK(argc>=2);
    setup(argc>3 && !strcmp(argv[3],"6"), argc>2 ? strtoul(argv[2],NULL,10) : 0);
    if (!strcmp(argv[1],"udp")) udp(0);
    else if (!strcmp(argv[1],"udp-preflight")) udp(1);
    else if (!strcmp(argv[1],"tcp")) tcp();
    else if (!strcmp(argv[1],"refused")) refused(1);
    else if (!strcmp(argv[1],"refused-blocking")) refused(0);
    else if (!strcmp(argv[1],"fork-server")) fork_server();
    else if (!strcmp(argv[1],"host-server")) host_server();
    else if (!strcmp(argv[1],"eintr")) signal_wait(0);
    else if (!strcmp(argv[1],"restart")) signal_wait(1);
    else if (!strcmp(argv[1],"sigpipe")) sigpipe();
    else if (!strcmp(argv[1],"cancel")) cancel(0);
    else if (!strcmp(argv[1],"cancel-fault")) cancel(1);
    else if (!strcmp(argv[1],"dns")) dns();
    else if (!strcmp(argv[1],"policy")) { CHECK(argc>2); policy(argv[2]); }
    else if (!strcmp(argv[1],"socket-abi")) socket_abi();
    else CHECK(0);
    return 0;
}
