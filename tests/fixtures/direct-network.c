// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void wait_read(int fd) { struct pollfd p = {fd, POLLIN, 0}; assert(poll(&p, 1, 5000) == 1 && (p.revents & POLLIN)); }
static void exact(int fd, unsigned char *p, size_t n) {
    while (n) { wait_read(fd); ssize_t count = recv(fd, p, n, 0); assert(count > 0); p += count; n -= (size_t)count; }
}
static void dns(void) {
    unsigned char query[] = {0x21,0x43,1,0,0,1,0,0,0,0,0,0,9,'l','o','c','a','l','h','o','s','t',0,0,1,0,1};
    struct sockaddr_in ns = {.sin_family = AF_INET, .sin_port = htons(53)};
    assert(inet_pton(AF_INET, "127.0.0.53", &ns.sin_addr) == 1);
    unsigned char reply[1024];
    int udp = socket(AF_INET, SOCK_DGRAM, 0); assert(udp >= 0);
    assert(sendto(udp, query, sizeof query, 0, (void *)&ns, sizeof ns) == sizeof query);
    wait_read(udp);
    struct sockaddr_in peer; socklen_t length = sizeof peer;
    ssize_t n = recvfrom(udp, reply, sizeof reply, 0, (void *)&peer, &length);
    assert(n > (ssize_t)sizeof query && !memcmp(&peer, &ns, sizeof ns));
    assert(reply[0] == 0x21 && reply[1] == 0x43 && !(reply[3] & 15) && reply[7] > 0);
    close(udp);
    int tcp = socket(AF_INET, SOCK_STREAM, 0); assert(tcp >= 0);
    assert(connect(tcp, (void *)&ns, sizeof ns) == 0);
    length = sizeof peer; assert(getpeername(tcp, (void *)&peer, &length) == 0 && !memcmp(&peer, &ns, sizeof ns));
    unsigned char prefix[] = {0, sizeof query};
    assert(send(tcp, prefix, 1, 0) == 1 && send(tcp, prefix + 1, 1, 0) == 1);
    assert(send(tcp, query, 5, 0) == 5 && send(tcp, query + 5, sizeof query - 5, 0) == sizeof query - 5);
    assert(shutdown(tcp, SHUT_WR) == 0);
    exact(tcp, prefix, 2); unsigned count = prefix[0] * 256u + prefix[1]; assert(count <= sizeof reply);
    exact(tcp, reply, count); assert(reply[0] == 0x21 && reply[1] == 0x43 && !(reply[3] & 15));
    assert(recv(tcp, reply, sizeof reply, 0) == 0); close(tcp);
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *addresses;
    assert(getaddrinfo("localhost", "80", &hints, &addresses) == 0);
    assert(addresses && ((struct sockaddr_in *)addresses->ai_addr)->sin_addr.s_addr == htonl(INADDR_LOOPBACK));
    freeaddrinfo(addresses);
    puts("direct DNS UDP/TCP framing, peer identity, half-close and musl getaddrinfo: ok");
}
static unsigned checksum(const unsigned char *p, size_t size) {
    unsigned sum = 0;
    for (size_t i = 0; i < size; i += 2) sum += p[i] * 256u + (i + 1 < size ? p[i + 1] : 0);
    while (sum >> 16) sum = (sum & 65535) + (sum >> 16);
    return (~sum) & 65535;
}
static int icmp(int ipv6) {
    int fd = socket(ipv6 ? AF_INET6 : AF_INET, SOCK_RAW, ipv6 ? 58 : 1);
    if (fd < 0 && (errno == EPERM || errno == EACCES)) return 77;
    assert(fd >= 0);
    int kind; socklen_t length = sizeof kind;
    assert(getsockopt(fd, SOL_SOCKET, SO_TYPE, &kind, &length) == 0 && kind == SOCK_RAW);
    int ttl = 47;
    assert(setsockopt(fd, ipv6 ? 41 : 0, ipv6 ? 16 : 2, &ttl, sizeof ttl) == 0);
    if (ipv6) {
        unsigned filter[8]; memset(filter, 255, sizeof filter); filter[129 / 32] &= ~(1u << (129 % 32));
        assert(setsockopt(fd, 58, 1, filter, sizeof filter) == 0);
        int offset = 2; assert(setsockopt(fd, 255, 7, &offset, sizeof offset) == 0);
    }
    struct sockaddr_storage address = {0}; socklen_t size;
    if (ipv6) { struct sockaddr_in6 *a = (void *)&address; a->sin6_family = AF_INET6; a->sin6_addr = in6addr_loopback; size = sizeof *a; }
    else { struct sockaddr_in *a = (void *)&address; a->sin_family = AF_INET; a->sin_addr.s_addr = htonl(INADDR_LOOPBACK); size = sizeof *a; }
    unsigned char request[] = {ipv6 ? 128 : 8, 0, 0, 0, 0x12, 0x34, 0, 9, 't', 'r', 'a', 'm', 'e', 's'};
    if (!ipv6) { unsigned sum = checksum(request, sizeof request); request[2] = sum >> 8; request[3] = sum; }
    assert(sendto(fd, request, sizeof request, 0, (void *)&address, size) == sizeof request);
    wait_read(fd);
    unsigned char reply[2048]; ssize_t n = recv(fd, reply, sizeof reply, 0); assert(n >= (ssize_t)sizeof request);
    size_t offset = ipv6 ? 0 : (reply[0] & 15) * 4u;
    assert((size_t)n == offset + sizeof request && reply[offset] == (ipv6 ? 129 : 0));
    assert(!memcmp(reply + offset + 4, request + 4, sizeof request - 4));
    if (!ipv6) { assert(offset >= 20 && reply[8] > 0); assert(checksum(reply, offset) == 0 && checksum(reply + offset, n - offset) == 0); }
    else {
        // IPv6 ::1/::1 pseudo-header verifies ID/checksum restoration.
        unsigned char packet[40 + sizeof request] = {0}; packet[15] = packet[31] = 1;
        packet[35] = sizeof request; packet[39] = 58; memcpy(packet + 40, reply, sizeof request);
        assert(checksum(packet, sizeof packet) == 0);
    }
    close(fd); puts("native ICMP echo, raw ABI, identifier and checksum: ok"); return 0;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "dns")) { dns(); return 0; }
    if (!strcmp(argv[1], "icmp4") || !strcmp(argv[1], "icmp6")) return icmp(!strcmp(argv[1], "icmp6"));
    int ipv6 = !strcmp(argv[1], "http6"), datagram = !strcmp(argv[1], "udp");
    int fd = socket(ipv6 ? AF_INET6 : AF_INET, datagram ? SOCK_DGRAM : SOCK_STREAM, 0); assert(fd >= 0);
    struct sockaddr_storage local = {0}; socklen_t size;
    if (ipv6) { struct sockaddr_in6 *p = (void *)&local; p->sin6_family = AF_INET6; size = sizeof *p; }
    else { struct sockaddr_in *p = (void *)&local; p->sin_family = AF_INET; size = sizeof *p; }
    assert(bind(fd, (void *)&local, size) == 0);
    if (!datagram) assert(listen(fd, 8) == 0);
    assert(getsockname(fd, (void *)&local, &size) == 0);
    unsigned port = ntohs(ipv6 ? ((struct sockaddr_in6 *)&local)->sin6_port : ((struct sockaddr_in *)&local)->sin_port);
    printf("PORT:%u\n", port); fflush(stdout);
    if (datagram) {
        char data[128]; struct sockaddr_storage source; socklen_t n = sizeof source;
        ssize_t count = recvfrom(fd, data, sizeof data, 0, (void *)&source, &n); assert(count > 0);
        assert(sendto(fd, data, (size_t)count, 0, (void *)&source, n) == count);
    } else {
        for (int i = 0; i < 2; i++) {
            int peer = accept(fd, NULL, NULL); assert(peer >= 0);
            char data[2048] = {0}; size_t count = 0;
            while (!strstr(data, "\r\n\r\n")) { ssize_t n = recv(peer, data + count, sizeof data - 1 - count, 0); assert(n > 0); count += (size_t)n; }
            assert(!strncmp(data, "GET / HTTP/1.", 13));
            const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\nConnection: close\r\n\r\nDIRECT_GUEST\n";
            size_t sent = 0;
            while (sent < sizeof response - 1) { ssize_t n = send(peer, response + sent, sizeof response - 1 - sent, MSG_NOSIGNAL); assert(n > 0); sent += (size_t)n; }
            close(peer);
        }
    }
    close(fd); puts("direct inbound listener: ok"); return 0;
}
