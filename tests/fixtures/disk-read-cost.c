// SPDX-License-Identifier: GPL-3.0-or-later
// Two fixed-work pread callers, no native-fill thread or elapsed-time loop.
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static pthread_barrier_t start;
static int fd;
static unsigned iterations;
static uint32_t checksums[2];
static void *reader(void *argument) {
    unsigned index = (unsigned)(uintptr_t)argument;
    unsigned char *bytes = malloc(65536); assert(bytes);
    int rc = pthread_barrier_wait(&start);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    uint32_t crc = 0;
    for (unsigned i = 0; i < iterations; i++) {
        assert(pread(fd, bytes, 65536, 0) == 65536);
        for (unsigned j = 0; j < 65536; j += 4093) {
            assert(bytes[j] == (unsigned char)(j * 13 + 7));
            crc = crc * 33 + bytes[j];
        }
    }
    checksums[index] = crc;
    free(bytes);
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 3 && !strcmp(argv[1], "io-only"));
    iterations = (unsigned)strtoul(argv[2], NULL, 10);
    assert(iterations && iterations <= 65536);
    int rc = mkdir("/tmp", 0777); assert(!rc || errno == EEXIST);
    char path[] = "/tmp/trames-read-cost-XXXXXX";
    fd = mkstemp(path); assert(fd >= 0 && !unlink(path));
    unsigned char *bytes = malloc(65536); assert(bytes);
    for (unsigned i = 0; i < 65536; i++) bytes[i] = (unsigned char)(i * 13 + 7);
    assert(write(fd, bytes, 65536) == 65536); free(bytes);
    assert(!pthread_barrier_init(&start, NULL, 2));
    pthread_t peer; assert(!pthread_create(&peer, NULL, reader, (void *)(uintptr_t)1));
    reader(NULL);
    assert(!pthread_join(peer, NULL));
    assert(!pthread_barrier_destroy(&start));
    assert(!close(fd));
    printf("disk-read-cost %u %08x %08x\n", iterations, checksums[0], checksums[1]);
    return 0;
}
