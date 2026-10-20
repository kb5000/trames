// SPDX-License-Identifier: GPL-3.0-or-later
// Linux host-only diagnostic when ptrace is unavailable. Count calls made by
// trames-disk threads, never guest syscalls. Do not use interposed runs as timing.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { OPENS, STATS, SEEKS, READS, PREADS, CLOSES, COUNT };
static _Atomic unsigned long counts[COUNT];
static _Thread_local int disk_thread = -1;
static void record(unsigned index) {
    if (disk_thread < 0) {
        char name[16] = {0};
        disk_thread = !pthread_getname_np(pthread_self(), name, sizeof name)
            && !strcmp(name, "trames-disk");
    }
    if (disk_thread) atomic_fetch_add_explicit(&counts[index], 1, memory_order_relaxed);
}
#define SYMBOL(name) static __typeof__(name) *real_##name
SYMBOL(open64); SYMBOL(fstat64); SYMBOL(lstat64); SYMBOL(statx);
SYMBOL(lseek64); SYMBOL(read); SYMBOL(pread64); SYMBOL(close);
__attribute__((constructor)) static void setup(void) {
#define RESOLVE(name) real_##name = dlsym(RTLD_NEXT, #name)
    RESOLVE(open64); RESOLVE(fstat64); RESOLVE(lstat64); RESOLVE(statx);
    RESOLVE(lseek64); RESOLVE(read); RESOLVE(pread64); RESOLVE(close);
}
int open64(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list args; va_start(args, flags); mode = va_arg(args, mode_t); va_end(args);
    }
    record(OPENS); return real_open64(path, flags, mode);
}
int fstat64(int fd, struct stat64 *out) { record(STATS); return real_fstat64(fd, out); }
int lstat64(const char *path, struct stat64 *out) { record(STATS); return real_lstat64(path, out); }
int statx(int fd, const char *path, int flags, unsigned mask, struct statx *out) {
    record(STATS); return real_statx(fd, path, flags, mask, out);
}
off64_t lseek64(int fd, off64_t offset, int whence) {
    record(SEEKS); return real_lseek64(fd, offset, whence);
}
ssize_t read(int fd, void *out, size_t count) { record(READS); return real_read(fd, out, count); }
ssize_t pread64(int fd, void *out, size_t count, off64_t offset) {
    record(PREADS); return real_pread64(fd, out, count, offset);
}
int close(int fd) { record(CLOSES); return real_close(fd); }
__attribute__((destructor)) static void report(void) {
    fprintf(stderr, "disk-io-counts opens=%lu stats=%lu seeks=%lu reads=%lu preads=%lu closes=%lu\n",
        counts[OPENS], counts[STATS], counts[SEEKS], counts[READS], counts[PREADS], counts[CLOSES]);
}
