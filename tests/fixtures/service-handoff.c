// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#ifdef TRAMES_HOST_ORACLE
static void *native_fill(void *p, int c, size_t n) { return memset(p, c, n); }
#else
extern void *native_fill(void *, int, size_t);
#endif
#define READERS 6u
#define ROUNDS 4u
#define CHUNK 65536u
#define FILLS 32u
static pthread_barrier_t start;
static int fd, mode;
static _Atomic uint64_t seen;
static uint32_t sums[READERS], native_checksum;
static void start_together(void) {
    int rc = pthread_barrier_wait(&start);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
}
static void *reader(void *arg) {
    unsigned id = (unsigned)(uintptr_t)arg;
    unsigned char *bytes = malloc(CHUNK); assert(bytes);
    start_together();
    for (unsigned i = 0; i < ROUNDS; i++) {
        unsigned expected = i * READERS + id;
        ssize_t rc;
        do {
            if (mode == 1) {
                struct iovec out[] = {{bytes, 8193}, {bytes + 8193, CHUNK - 8193}};
                rc = preadv(fd, out, 2, (off_t)expected * CHUNK);
            } else if (mode == 2) { rc = read(fd, bytes, CHUNK); }
            else { rc = pread(fd, bytes, CHUNK, (off_t)expected * CHUNK); }
        } while (rc == -1 && errno == EINTR);
        assert(rc == CHUNK);
        unsigned index;
        memcpy(&index, bytes, sizeof index);
        assert(index < READERS * ROUNDS && (mode == 2 || index == expected));
        assert(!(atomic_fetch_or_explicit(&seen, UINT64_C(1) << index, memory_order_relaxed) & (UINT64_C(1) << index)));
        for (unsigned j = 4; j < CHUNK; j += 4093)
            assert(bytes[j] == (unsigned char)(index * 17 + j * 13 + 7));
        sums[id] += index;
    }
    free(bytes);
    return NULL;
}
static void *memory_worker(void *unused) {
    (void)unused;
    unsigned char *bytes = malloc(1u << 20); assert(bytes);
    start_together();
    for (unsigned i = 0; i < FILLS; i++) {
        unsigned char value = (unsigned char)(i * 31 + 7);
        assert(native_fill(bytes, value, 1u << 20) == bytes);
        assert(bytes[0] == value && bytes[524287] == value && bytes[1048575] == value);
        native_checksum = native_checksum * 33 + value;
    }
    free(bytes);
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    mode = !strcmp(argv[1], "preadv") ? 1 : !strcmp(argv[1], "read") ? 2 : 0;
    assert(mode || !strcmp(argv[1], "pread"));
    int pinned = !strcmp(argv[2], "pinned");
    assert(pinned || !strcmp(argv[2], "spread"));
#ifndef TRAMES_HOST_ORACLE
    if (pinned) {
        cpu_set_t cpus; CPU_ZERO(&cpus); CPU_SET(0, &cpus);
        assert(!sched_setaffinity(0, sizeof cpus, &cpus));
    }
#endif
    int rc = mkdir("/tmp", 0777); assert(!rc || errno == EEXIST);
    char path[] = "/tmp/trames-service-handoff-XXXXXX";
    fd = mkstemp(path); assert(fd >= 0 && !unlink(path));
    unsigned char *bytes = malloc(CHUNK); assert(bytes);
    for (unsigned index = 0; index < READERS * ROUNDS; index++) {
        for (unsigned j = 0; j < CHUNK; j++) bytes[j] = (unsigned char)(index * 17 + j * 13 + 7);
        memcpy(bytes, &index, sizeof index);
        assert(write(fd, bytes, CHUNK) == CHUNK);
    }
    free(bytes);
    assert(!lseek(fd, 0, SEEK_SET));
    assert(!pthread_barrier_init(&start, NULL, READERS + 1));
    pthread_t threads[READERS], native;
    for (unsigned id = 0; id < READERS; id++) assert(!pthread_create(&threads[id], NULL, reader, (void *)(uintptr_t)id));
    assert(!pthread_create(&native, NULL, memory_worker, NULL));
    unsigned sum = 0;
    for (unsigned id = 0; id < READERS; id++) { assert(!pthread_join(threads[id], NULL)); sum += sums[id]; }
    assert(!pthread_join(native, NULL));
    assert(!pthread_barrier_destroy(&start));
    assert(atomic_load(&seen) == (UINT64_C(1) << (READERS * ROUNDS)) - 1);
    assert(lseek(fd, 0, SEEK_CUR) == (mode == 2 ? READERS * ROUNDS * CHUNK : 0));
    assert(!close(fd));
    printf("service-handoff %s %s %u %08x\n", argv[1], argv[2], sum, native_checksum);
    return 0;
}
