// SPDX-License-Identifier: GPL-3.0-or-later
// Fixed work: a native memory thread overlaps ordinary FD/stream syscalls.
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef TRAMES_HOST_ORACLE
static void *native_fill(void *p, int c, size_t n) { return memset(p, c, n); }
#else
extern void *native_fill(void *, int, size_t);
#endif
static pthread_barrier_t start;
static uint32_t checksum;
static unsigned iterations = 1024;
static void *memory_worker(void *unused) {
    (void)unused;
    size_t n = 1024 * 1024;
    unsigned char *bytes = malloc(n);
    assert(bytes);
    int rc = pthread_barrier_wait(&start);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned i = 0; i < iterations; i++) {
        unsigned char value = (unsigned char)(i * 31 + 7);
        assert(native_fill(bytes, value, n) == bytes);
        assert(bytes[0] == value && bytes[n / 2] == value && bytes[n - 1] == value);
        checksum = checksum * 33 + value;
    }
    free(bytes);
    return NULL;
}
int main(int argc, char **argv) {
    int streams = argc > 1 && !strcmp(argv[1], "pipe");
    int sockets = argc > 1 && !strcmp(argv[1], "unix");
    int private_map = argc > 1 && !strcmp(argv[1], "mapped-read");
    int shared_map = argc > 1 && !strcmp(argv[1], "mapped-shared");
    int mapped = private_map || shared_map;
    int vectors = argc > 1 && !strcmp(argv[1], "vector");
    int files = vectors || mapped || (argc > 1 && !strcmp(argv[1], "file"));
    if (argc > 2) { iterations = (unsigned)strtoul(argv[2], NULL, 10); assert(iterations > 0 && iterations <= 8192); }
    char temporary[] = "/tmp/trames-epoch-XXXXXX";
    if (files) { int rc = mkdir("/tmp", 0777); assert(rc == 0 || errno == EEXIST); }
    int fd = files ? mkstemp(temporary) : open("/dev/null", O_RDWR);
    assert(fd >= 0);
    if (files) assert(!unlink(temporary));
    unsigned char *mapping = NULL;
    if (mapped) {
        assert(!ftruncate(fd, 4096));
        mapping = mmap(NULL, 4096, PROT_READ | PROT_WRITE, shared_map ? MAP_SHARED : MAP_PRIVATE, fd, 0);
        assert(mapping != MAP_FAILED);
    }
    int ends[2];
    if (sockets) assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, ends));
    else assert(!pipe(ends));
    assert(!pthread_barrier_init(&start, NULL, 2));
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, memory_worker, NULL));
    int rc = pthread_barrier_wait(&start);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned i = 0; i < iterations * 8; i++) {
        if (mapped) {
            unsigned char output[16];
            mapping[0] = (unsigned char)i;
            assert(pread(fd, output, 16, 0) == 16);
            assert(output[0] == (shared_map ? (unsigned char)i : 0));
            for (unsigned j = 1; j < 16; j++) assert(output[j] == 0);
        } else if (files) {
            unsigned char input[16], output[16];
            memset(input, i & 255, sizeof input);
            struct iovec in[] = {{input, 8}, {input + 8, 8}};
            struct iovec out[] = {{output, 5}, {output + 5, 11}};
            assert(lseek(fd, 0, SEEK_SET) == 0);
            if (vectors) {
                assert(writev(fd, in, 2) == 16);
                assert(preadv(fd, out, 2, 0) == 16);
                assert(!memcmp(input, output, 16));
                assert(lseek(fd, 0, SEEK_CUR) == 16);
                assert(pwritev(fd, in, 2, 0) == 16);
                assert(lseek(fd, 0, SEEK_SET) == 0);
                assert(readv(fd, out, 2) == 16);
            } else {
                assert(write(fd, input, 16) == 16);
                assert(pread(fd, output, 16, 0) == 16);
                assert(!memcmp(input, output, 16));
                assert(lseek(fd, 0, SEEK_CUR) == 16);
                assert(pwrite(fd, input, 16, 0) == 16);
                assert(lseek(fd, 0, SEEK_SET) == 0);
                assert(read(fd, output, 16) == 16);
            }
            assert(!memcmp(input, output, 16));
            if (!(i % 1024)) { assert(!fsync(fd)); assert(!fdatasync(fd)); }
        } else if (streams || sockets) {
            unsigned char input[16], output[16];
            memset(input, i & 255, sizeof input);
            struct iovec in[] = {{input, 5}, {input+5, 11}};
            struct iovec out[] = {{output, 8}, {output+8, 8}};
            if (sockets && i % 2) {
                assert(writev(ends[1], in, 2) == 16);
                assert(readv(ends[0], out, 2) == 16);
            } else {
                assert(write(ends[1], input, sizeof input) == sizeof input);
                assert(read(ends[0], output, sizeof output) == sizeof output);
            }
            assert(!memcmp(input, output, sizeof input));
        } else {
            struct stat s;
            assert(fcntl(fd, F_GETFL) >= 0);
            assert(fcntl(fd, F_GETFD) == 0);
            assert(!fstat(fd, &s) && S_ISCHR(s.st_mode));
            assert(lseek(fd, 0, SEEK_SET) == 0);
        }
        // Real mapping edits and FD-table changes must still rendezvous. Mixing
        // them with service calls exercises view lifetime and bitmap refresh.
        if (!(i % 1024)) {
            void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            assert(p != MAP_FAILED);
            *(volatile unsigned char *)p = (unsigned char)i;
            assert(!munmap(p, 4096));
            int copy = dup(ends[0]); assert(copy >= 0); assert(!close(copy));
        }
    }
    assert(!pthread_join(worker, NULL));
    if (mapped) assert(!munmap(mapping, 4096));
    assert(!pthread_barrier_destroy(&start));
    assert(!close(fd) && !close(ends[0]) && !close(ends[1]));
    const char *mode = mapped ? (shared_map ? "mapped-shared" : "mapped-read") : files ? (vectors ? "vector" : "file") : sockets ? "unix" : streams ? "pipe" : "metadata";
    printf("epoch-services %s %u %08x\n", mode, iterations, checksum);
    return 0;
}
