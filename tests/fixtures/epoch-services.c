// SPDX-License-Identifier: GPL-3.0-or-later
// Fixed work: a native memory thread overlaps ordinary FD/stream syscalls.
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
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
    if (argc > 2) { iterations = (unsigned)strtoul(argv[2], NULL, 10); assert(iterations > 0 && iterations <= 8192); }
    int fd = open("/dev/null", O_RDWR);
    assert(fd >= 0);
    int ends[2];
    assert(!pipe(ends));
    assert(!pthread_barrier_init(&start, NULL, 2));
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, memory_worker, NULL));
    int rc = pthread_barrier_wait(&start);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned i = 0; i < iterations * 8; i++) {
        if (streams) {
            unsigned char input[16], output[16];
            memset(input, i & 255, sizeof input);
            assert(write(ends[1], input, sizeof input) == sizeof input);
            assert(read(ends[0], output, sizeof output) == sizeof output);
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
    assert(!pthread_barrier_destroy(&start));
    assert(!close(fd) && !close(ends[0]) && !close(ends[1]));
    printf("epoch-services %s %u %08x\n", streams ? "pipe" : "metadata", iterations, checksum);
    return 0;
}
