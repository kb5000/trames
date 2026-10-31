// SPDX-License-Identifier: GPL-3.0-or-later
// Identical fixed-work cached file I/O on macOS, native Linux and Trames.
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#ifdef __linux__
#include <sys/inotify.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#endif
#include <time.h>
#include <unistd.h>
static double now(void) {
    struct timespec time;
    assert(!clock_gettime(CLOCK_MONOTONIC, &time));
    return time.tv_sec + time.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
    assert(argc == 4 || argc == 5);
    const char *path = argv[1], *mode = argv[2];
    unsigned iterations = (unsigned)strtoul(argv[3], NULL, 10);
    assert(iterations && iterations <= 1000000);
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600); assert(fd >= 0);
    const size_t length = 8 * 1024 * 1024;
    unsigned char buffer[65536];
    for (size_t i = 0; i < sizeof(buffer); i++) buffer[i] = (unsigned char)(i * 13 + 7);
    assert(!ftruncate(fd, length));
    for (size_t offset = 0; offset < length; offset += sizeof(buffer)) {
        assert(pwrite(fd, buffer, sizeof(buffer), offset) == sizeof(buffer));
    }
    assert(!fsync(fd));
    int watch = -1;
    if (argc == 5 && !strcmp(argv[4], "watch")) {
#ifdef __linux__
        watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC); assert(watch >= 0);
        assert(inotify_add_watch(watch, path, IN_MODIFY) >= 0);
#elif defined(__APPLE__)
        watch = kqueue(); assert(watch >= 0);
        struct kevent event;
        EV_SET(&event, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE | NOTE_EXTEND, 0, NULL);
        assert(!kevent(watch, &event, 1, NULL, 0, NULL));
#endif
    } else if (argc == 5) {
        assert(!strcmp(argv[4], "flock"));
        assert(!flock(fd, LOCK_EX | LOCK_NB));
    }
    size_t block = !strcmp(mode, "read4k") ? 4096 : sizeof(buffer);
    int reading = !strcmp(mode, "read64k") || !strcmp(mode, "read4k");
    int writing = !strcmp(mode, "write64k");
    assert(reading || writing || !strcmp(mode, "stat"));
    uint64_t checksum = 0;
    double began = now();
    for (unsigned i = 0; i < iterations; i++) {
        off_t offset = (off_t)((uint64_t)i * block % length);
        if (reading) {
            assert(pread(fd, buffer, block, offset) == (ssize_t)block);
            for (size_t byte = 0; byte < block; byte += 4093) {
                assert(buffer[byte] == (unsigned char)((offset + byte) * 13 + 7));
                checksum += buffer[byte];
            }
        } else if (writing) {
            assert(pwrite(fd, buffer, block, offset) == (ssize_t)block);
        } else {
            struct stat stat;
            assert(!fstatat(AT_FDCWD, path, &stat, 0));
            assert(stat.st_size == (off_t)length && S_ISREG(stat.st_mode));
            checksum += stat.st_size;
        }
    }
    double seconds = now() - began;
    began = now(); assert(!fsync(fd)); double sync_seconds = now() - began;
    if (writing) {
        for (size_t offset = 0; offset < length; offset += sizeof(buffer)) {
            assert(pread(fd, buffer, sizeof(buffer), offset) == sizeof(buffer));
            for (size_t byte = 0; byte < sizeof(buffer); byte++) {
                assert(buffer[byte] == (unsigned char)(byte * 13 + 7));
            }
        }
    }
    if (watch >= 0) assert(!close(watch));
    assert(!close(fd));
    printf("{\"mode\":\"%s\",\"operations\":%u,\"seconds\":%.9f,\"mib_per_second\":%.3f,\"ops_per_second\":%.3f,\"sync_seconds\":%.9f,\"checksum\":%llu}\n",
        mode, iterations, seconds, reading || writing ? (double)iterations * block / 1048576 / seconds : 0,
        iterations / seconds, sync_seconds, (unsigned long long)checksum);
    return 0;
}
