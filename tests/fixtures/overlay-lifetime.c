// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static void directory(const char *path) {
    assert(mkdir(path, 0755) == 0 || errno == EEXIST);
}
int main(void) {
    directory("/lower"); directory("/upper"); directory("/work"); directory("/merged");
    int fd = open("/lower/file", O_CREAT | O_TRUNC | O_WRONLY, 0644);
    assert(fd >= 0 && write(fd, "lower", 5) == 5 && close(fd) == 0);
    for (unsigned round = 0; round < 24; ++round) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) {
            assert(unshare(CLONE_NEWNS) == 0);
            if (mount("overlay", "/merged", "overlay", 0,
                      "lowerdir=/lower,upperdir=/upper,workdir=/work,index=off")) {
                perror("overlay workdir reuse"); _exit(1);
            }
            directory("/merged/old");
            assert(syscall(SYS_pivot_root, "/merged", "/merged/old") == 0);
            assert(chdir("/") == 0);
            char bytes[5]; fd = open("/file", O_RDONLY);
            assert(fd >= 0 && read(fd, bytes, 5) == 5 && !memcmp(bytes, "lower", 5));
            assert(close(fd) == 0);
            _exit(0);
        }
        int status; assert(waitpid(child, &status, 0) == child);
        if (!WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr, "overlay namespace failed at round %u, status=%d\n", round, status);
            return 1;
        }
    }
    puts("overlay lifetime: repeated namespace pivot and workdir reuse: ok");
    return 0;
}
