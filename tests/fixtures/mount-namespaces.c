// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static void waited(pid_t pid) { int s; assert(waitpid(pid, &s, 0) == pid && WIFEXITED(s) && WEXITSTATUS(s) == 0); }
static void directory(const char *p) { assert(mkdir(p, 0755) == 0); }
static int file(const char *p) { int fd = open(p, O_CREAT | O_RDWR, 0755); assert(fd >= 0); assert(write(fd, "guest", 5) == 5); return fd; }
static void contents(const char *p) { char b[8] = {0}; int fd = open(p, O_RDONLY); assert(fd >= 0); assert(read(fd, b, sizeof b) == 5 && !strcmp(b, "guest")); close(fd); }
static int clone_mount(void *unused) {
    (void)unused;
    assert(mount("tmpfs", "/isolated", "tmpfs", 0, NULL) == 0);
    close(file("/isolated/cloned"));
    return 0;
}
static char child_stack[65536] __attribute__((aligned(16)));
int main(void) {
    directory("/source"); directory("/target"); directory("/isolated"); directory("/newroot");
    close(file("/source/data"));
    int original = open("/proc/self/ns/mnt", O_RDONLY); assert(original >= 0);
    assert(ioctl(original, 0xb703) == CLONE_NEWNS);
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        assert(unshare(CLONE_NEWNS) == 0);
        assert(mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL) == 0);
        assert(mount("tmpfs", "/isolated", "tmpfs", MS_NOSUID | MS_NODEV, "size=4k,mode=0700") == 0);
        int fd = file("/isolated/inside"); assert(ftruncate(fd, 4096) == 0);
        assert(pwrite(fd, "x", 1, 4096) == -1 && errno == ENOSPC);
        struct statfs fs; assert(fstatfs(fd, &fs) == 0 && fs.f_type == 0x01021994);
        close(fd); assert(setns(original, CLONE_NEWNS) == 0);
        assert(access("/isolated/inside", F_OK) == -1 && errno == ENOENT);
        _exit(0);
    }
    waited(child); assert(access("/isolated/inside", F_OK) == -1 && errno == ENOENT);
    child = clone(clone_mount, child_stack + sizeof child_stack, CLONE_NEWNS | SIGCHLD, NULL);
    assert(child > 0); waited(child);
    assert(access("/isolated/cloned", F_OK) == -1 && errno == ENOENT);
    assert(clone(clone_mount, child_stack + sizeof child_stack, CLONE_NEWNS | CLONE_FS | SIGCHLD, NULL) == -1 && errno == EINVAL);
    assert(mount("/source", "/target", NULL, MS_BIND, NULL) == 0);
    contents("/target/data");
    struct stat a, b; assert(stat("/source/data", &a) == 0 && stat("/target/data", &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino);
    int dir = open("/target", O_DIRECTORY | O_RDONLY); assert(dir >= 0);
    assert(chdir("/target") == 0 && chdir("..") == 0);
    char cwd[128]; assert(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/"));
    assert(mount(NULL, "/target", NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOEXEC, NULL) == 0);
    assert(open("/target/data", O_WRONLY) == -1 && errno == EROFS);
    assert(chmod("/target/data", 0600) == -1 && errno == EROFS);
    int fd = open("/target/data", O_RDONLY); assert(fd >= 0);
    assert(mmap(NULL, 4096, PROT_READ | PROT_EXEC, MAP_PRIVATE, fd, 0) == MAP_FAILED && errno == EPERM); close(fd);
    fd = open("/source/data", O_RDWR); assert(fd >= 0); close(fd);
    assert(umount("/target") == -1 && errno == EBUSY);
    assert(umount2("/target", MNT_DETACH) == 0);
    assert(access("/target/data", F_OK) == -1 && errno == ENOENT);
    fd = openat(dir, "data", O_RDONLY); assert(fd >= 0); close(fd); close(dir);
    // Reuse a number previously visible in a proc snapshot: mount must refresh
    // it and follow the actual directory capability through /proc/self/fd.
    fd=open("/source/data",O_RDONLY); assert(fd>=0);
    char procfd[80], label[128]; snprintf(procfd,sizeof procfd,"/proc/self/fd/%d",fd);
    assert(readlink(procfd,label,sizeof label)>0); close(fd);
    dir=open("/target",O_PATH|O_DIRECTORY); assert(dir==fd);
    assert(rename("/target","/moved-target")==0); directory("/target");
    assert(mount("tmpfs",procfd,"tmpfs",MS_NOSUID,NULL)==0);
    fd=file("/moved-target/magic"); close(fd); assert(access("/target/magic",F_OK)==-1 && errno==ENOENT);
    close(dir); assert(umount2("/moved-target",MNT_DETACH)==0);
    puts("mount: namespace isolation, setns, tmpfs quota, bind identity/readonly/noexec, detached fd: ok");

    child = fork(); assert(child >= 0);
    if (!child) {
        assert(unshare(CLONE_NEWNS) == 0);
        assert(mount("/source", "/newroot", NULL, MS_BIND, NULL) == 0);
        directory("/newroot/old"); directory("/newroot/proc");
        assert(mount("proc", "/newroot/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0);
        assert(syscall(SYS_pivot_root, "/newroot", "/newroot/old") == 0);
        assert(getcwd(cwd, sizeof cwd) && !strcmp(cwd, "/"));
        contents("/data"); contents("/old/source/data");
        assert(umount2("/old", MNT_DETACH) == 0);
        assert(access("/old/source", F_OK) == -1 && errno == ENOENT);
        char buf[4096]; fd = open("/proc/self/mountinfo", O_RDONLY); assert(fd >= 0);
        ssize_t n = read(fd, buf, sizeof buf - 1); assert(n > 0); buf[n] = 0; close(fd);
        assert(strstr(buf, " /proc ") && !strstr(buf, " /old "));
        _exit(0);
    }
    waited(child); contents("/source/data");
    child=fork(); assert(child>=0);
    if(!child) {
        assert(unshare(CLONE_NEWNS)==0);
        assert(mount("/source","/newroot",NULL,MS_BIND,NULL)==0);
        int oldroot=open("/",O_PATH|O_DIRECTORY), newroot=open("/newroot",O_RDONLY|O_DIRECTORY);
        assert(oldroot>=0 && newroot>=0 && fchdir(newroot)==0);
        // Original crun stacks the old root on the new root, then detaches it
        // through a retained old-root FD rather than creating a put_old folder.
        assert(syscall(SYS_pivot_root,".",".")==0 && fchdir(oldroot)==0);
        assert(mount(NULL,".",NULL,MS_REC|MS_PRIVATE,NULL)==0 && umount2(".",MNT_DETACH)==0);
        assert(umount2(".",MNT_DETACH)==-1 && errno==EINVAL);
        assert(chdir("/")==0); contents("/data");
        assert(access("/source/data",F_OK)==-1 && errno==ENOENT);
        int retained=openat(oldroot,"source/data",O_RDONLY); assert(retained>=0); close(retained);
        close(oldroot); close(newroot); _exit(0);
    }
    waited(child); contents("/source/data"); close(original);
    puts("mount: pivot_root, mounted proc, old-root detach, parent root preserved: ok");
    return 0;
}
