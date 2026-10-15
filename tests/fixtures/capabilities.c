// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#define BIT(n) (UINT64_C(1) << (n))
#define ALL ((BIT(41))-1)
struct header { uint32_t version; int pid; };
struct word { uint32_t e, p, i; };
struct caps { uint64_t e, p, i; };
static struct caps get(int pid) {
    struct header h = {0x20080522, pid}; struct word d[2];
    assert(syscall(SYS_capget, &h, d) == 0);
    return (struct caps){d[0].e | ((uint64_t)d[1].e << 32), d[0].p | ((uint64_t)d[1].p << 32), d[0].i | ((uint64_t)d[1].i << 32)};
}
static int put(uint64_t e, uint64_t p, uint64_t i) {
    struct header h = {0x20080522, 0};
    struct word d[2] = {{e, p, i}, {e >> 32, p >> 32, i >> 32}};
    return syscall(SYS_capset, &h, d);
}
#define DENIED(expr, code) do { errno = 0; assert((expr) == -1 && errno == (code)); } while (0)
static long ctl(int op, unsigned long a, unsigned long b) { return prctl(op, a, b, 0UL, 0UL); }
static void waited(pid_t p) { int s; assert(waitpid(p, &s, 0) == p && WIFEXITED(s) && WEXITSTATUS(s) == 0); }
static void isolated(void (*fn)(void)) { pid_t p = fork(); assert(p >= 0); if (!p) { fn(); _exit(0); } waited(p); }
static void abi(void) {
    struct header h = {0, 0}; struct word d[2];
    assert(syscall(SYS_capget, &h, NULL) == 0 && h.version == 0x20080522);
    h.version = 0; DENIED(syscall(SYS_capget, &h, d), EINVAL); assert(h.version == 0x20080522);
    DENIED(syscall(SYS_capget, NULL, d), EFAULT);
    h.version = 0x19980330; assert(syscall(SYS_capget, &h, d) == 0 && d[0].e == UINT32_MAX);
    h.version = 0x20071026; assert(syscall(SYS_capget, &h, d) == 0 && d[1].e == 511);
    h.version = 0x20080522; h.pid = -1; DENIED(syscall(SYS_capget, &h, d), EINVAL);
    h.pid = 0; DENIED(syscall(SYS_capset, &h, NULL), EFAULT);
    void *guard = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0); assert(guard != MAP_FAILED);
    memset(guard, 0, 8192); assert(mprotect((char *)guard + 4096, 4096, PROT_NONE) == 0);
    DENIED(syscall(SYS_capset, &h, (char *)guard + 4096 - 12), EFAULT);
    assert(get(0).p == ALL); munmap(guard, 8192);
    DENIED(put(ALL, 0, 0), EPERM); assert(get(0).p == ALL);
    assert(put(ALL & ~BIT(8), ALL, 0) == 0);
    assert(put(BIT(13), BIT(13), 0) == 0);
    DENIED(put(ALL, ALL, 0), EPERM);
    DENIED(put(BIT(13), BIT(13), BIT(21)), EPERM);
    assert(put(BIT(13), BIT(13), BIT(13)) == 0);
}
static void v1_set(void) {
    struct header h = {0x19980330, 0}; struct word d = {UINT32_MAX, UINT32_MAX, 0};
    assert(syscall(SYS_capset, &h, &d) == 0);
    assert(get(0).e == UINT32_MAX && get(0).p == UINT32_MAX);
}
static void enforcement(void) {
    assert(put(0, ALL, 0) == 0); // UID remains zero: no root shortcut.
    DENIED(unshare(CLONE_NEWUTS), EPERM);
    DENIED(unshare(CLONE_NEWNS), EPERM);
    DENIED(unshare(CLONE_NEWPID), EPERM);
    DENIED(sethostname("no", 2), EPERM);
    DENIED(chroot("/"), EPERM);
    DENIED(mount("tmpfs", "/tmp", "tmpfs", 0, NULL), EPERM);
    DENIED(setuid(1000), EPERM); DENIED(setgid(1000), EPERM);
    DENIED(setgroups(0, NULL), EPERM);
    DENIED(socket(AF_INET, SOCK_RAW, 1), EPERM);
    DENIED(open("/secret", O_RDONLY), EACCES);
    DENIED(chmod("/secret", 0600), EPERM);
    DENIED(chown("/secret", 0, 0), EPERM);
    assert(put(BIT(1), ALL, 0) == 0);
    int f = open("/secret", O_RDONLY); assert(f >= 0); close(f);
    DENIED(access("/secret", X_OK), EACCES); // no execute bits, even with DAC_OVERRIDE
    assert(put(BIT(2), ALL, 0) == 0);
    f = open("/secret", O_RDONLY); assert(f >= 0); close(f);
    DENIED(open("/secret", O_WRONLY), EACCES);
    assert(put(BIT(21), ALL, 0) == 0 && unshare(CLONE_NEWUTS) == 0);
    assert(sethostname("caps", 4) == 0);
}
static void uid_transitions(void) {
    assert(ctl(8, 1, 0) == 0 && setuid(1000) == 0);
    assert(get(0).e == 0 && get(0).p == ALL);
    assert(put(BIT(7)|BIT(13), ALL, 0) == 0);
    assert(setuid(0) == 0 && get(0).e == ALL);
    assert(setfsuid(1000) == 0 && !(get(0).e & BIT(1)) && (get(0).e & BIT(13)));
    assert(setfsuid(0) == 1000 && get(0).e == ALL);
    assert(ctl(8, 0, 0) == 0 && setuid(1000) == 0 && get(0).p == 0);
    DENIED(setuid(0), EPERM);
}
static void locks(void) {
    assert(ctl(28, 16|32|64|128, 0) == 0);
    DENIED(ctl(8, 0, 0), EPERM); DENIED(ctl(28, 0, 0), EPERM);
    assert(put(ALL, ALL, BIT(13)) == 0);
    DENIED(ctl(47, 2, 13), EPERM);
    assert(ctl(24, 13, 0) == 0 && ctl(23, 13, 0) == 0);
    assert(get(0).p & BIT(13)); // bounding drop doesn't revoke existing authority
    DENIED(ctl(23, 41, 0), EINVAL);
    DENIED(ctl(38, 0, 0), EINVAL);
    assert(ctl(38, 1, 0) == 0 && ctl(39, 0, 0) == 1);
    DENIED(ctl(38, 0, 0), EINVAL);
}
static void fixup_and_inheritance(void) {
    assert(ctl(28, 4, 0) == 0 && setuid(1000) == 0);
    assert(get(0).p == ALL && get(0).e == ALL);
    assert(access("/secret", R_OK) == 0); // NO_SETUID_FIXUP keeps effective DAC caps.
    assert(put(0, ALL, 0) == 0); DENIED(access("/secret", R_OK), EACCES);
    assert(put(ALL, ALL, BIT(13)) == 0 && ctl(47, 2, 13) == 0);
    assert(put(ALL, ALL, 0) == 0 && ctl(47, 1, 13) == 0);
    assert(ctl(24, 13, 0) == 0);
    DENIED(put(ALL, ALL, BIT(13)), EPERM); // cannot newly inherit a dropped bounding bit
}
static void process_authority(void) {
    int ready[2], done[2]; assert(pipe(ready) == 0 && pipe(done) == 0);
    pid_t p = fork(); assert(p >= 0);
    if (!p) { assert(setuid(1000) == 0); char c='x'; assert(write(ready[1], &c, 1) == 1); assert(read(done[0], &c, 1) == 1); _exit(0); }
    char c; assert(read(ready[0], &c, 1) == 1);
    assert(put(0, ALL, 0) == 0); DENIED(kill(p, 0), EPERM);
    assert(put(BIT(5), ALL, 0) == 0 && kill(p, 0) == 0);
    struct rlimit r = {4096, 4096}; assert(setrlimit(RLIMIT_FSIZE, &r) == 0);
    r.rlim_cur = r.rlim_max = 8192; DENIED(setrlimit(RLIMIT_FSIZE, &r), EPERM);
    assert(put(BIT(24), ALL, 0) == 0 && setrlimit(RLIMIT_FSIZE, &r) == 0);
    assert(write(done[1], &c, 1) == 1); waited(p);
    close(ready[0]); close(ready[1]); close(done[0]); close(done[1]);
}
static void *thread_drop(void *unused) { (void)unused; assert(put(0, 0, 0) == 0); assert(get(0).p == 0); return NULL; }
static void threads(void) { pthread_t t; assert(pthread_create(&t, NULL, thread_drop, NULL) == 0); assert(pthread_join(t, NULL) == 0); assert(get(0).p == ALL); }
static void cross_pid(void) {
    int ready[2], done[2]; assert(pipe(ready) == 0 && pipe(done) == 0);
    pid_t p = fork(); assert(p >= 0);
    if (!p) { assert(put(BIT(13), BIT(13), 0) == 0); char c='x'; assert(write(ready[1], &c, 1) == 1); assert(read(done[0], &c, 1) == 1); _exit(0); }
    char c; assert(read(ready[0], &c, 1) == 1 && get(p).e == BIT(13));
    struct header h = {0x20080522, p}; struct word d[2] = {{0},{0}};
    DENIED(syscall(SYS_capset, &h, d), EPERM);
    assert(write(done[1], &c, 1) == 1); waited(p);
    close(ready[0]); close(ready[1]); close(done[0]); close(done[1]);
    assert(unshare(CLONE_NEWPID) == 0);
    p=fork(); assert(p >= 0); if (!p) { assert(getpid() == 1 && get(1).p == ALL); _exit(0); } waited(p);
}
static void do_exec(const char *path, const char *mode) { execl(path, path, mode, NULL); assert(0); }
static void nnp_exec(void) { assert(ctl(38, 1, 0) == 0 && setuid(1000) == 0); do_exec("/suid", "nnp"); }
static void suid_exec(void) { assert(setuid(1000) == 0); do_exec("/suid", "suid"); }
static void ambient_exec(void) {
    assert(ctl(8, 1, 0) == 0 && setuid(1000) == 0);
    assert(put(0, BIT(13), BIT(13)) == 0 && ctl(47, 2, 13) == 0);
    assert(ctl(47, 1, 13) == 1); do_exec("/caps", "ambient");
}
static void noroot_exec(void) { assert(ctl(28, 1|2, 0) == 0); do_exec("/caps", "noroot"); }
static void bound_exec(void) { assert(ctl(24, 13, 0) == 0); do_exec("/caps", "bound"); }
static void fsuid_exec(void) { assert(setfsuid(1000) == 0); do_exec("/caps", "fsuid"); }
static void sgid_exec(void) { do_exec("/sgid", "sgid"); }
static void inherit_bound_exec(void) { assert(put(ALL, ALL, BIT(13)) == 0 && ctl(24, 13, 0) == 0); do_exec("/caps", "inherit-bound"); }
static void nnp_id_exec(void) { assert(setresuid(1000, 0, 0) == 0 && ctl(38, 1, 0) == 0); do_exec("/caps", "nnp-id"); }
static void nnp_root_exec(void) { assert(put(0, 0, 0) == 0 && ctl(38, 1, 0) == 0); do_exec("/caps", "nnp-root"); }
static void abi_unprivileged(void) {
    assert(put(0, 0, 0) == 0);
    struct header h = {0, 0}; struct word d[2];
    assert(syscall(SYS_capget, &h, NULL) == 0 && h.version == 0x20080522);
    h.version=0; DENIED(syscall(SYS_capget, &h, d), EINVAL); assert(h.version == 0x20080522);
    assert(get(0).e == 0 && get(0).p == 0);
    h.pid=-1; DENIED(syscall(SYS_capset, &h, d), EPERM);
    h.pid=0;
    DENIED(put(BIT(13), BIT(13), 0), EPERM);
    DENIED(ctl(24, 13, 0), EPERM);
    DENIED(ctl(23, 41, 0), EINVAL);
    DENIED(ctl(47, 2, 13), EPERM);
    assert(ctl(47, 3, 13) == 0 && ctl(47, 4, 0) == 0);
    assert(ctl(38, 1, 0) == 0 && ctl(39, 0, 0) == 1);
    DENIED(ctl(38, 0, 0), EINVAL);
    DENIED(prctl(39, 0UL, 1UL, 0UL, 0UL), EINVAL);
    DENIED(ctl(28, 0, 0), EPERM);
    puts("native capability ABI: version negotiation, zero-capset, denial, ambient clear and irreversible NNP: ok");
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "abi-unprivileged")) { abi_unprivileged(); return 0; }
    if (argc == 2) {
        struct caps c=get(0);
        if (!strcmp(argv[1], "nnp")) { assert(geteuid() == 1000 && c.p == 0 && ctl(39, 0, 0) == 1); }
        else if (!strcmp(argv[1], "suid")) { assert(getuid() == 1000 && geteuid() == 0 && c.p == ALL && c.e == ALL); }
        else if (!strcmp(argv[1], "ambient")) { assert(geteuid() == 1000 && c.e == BIT(13) && c.p == BIT(13) && c.i == BIT(13) && ctl(47, 1, 13) == 1 && ctl(7, 0, 0) == 0); }
        else if (!strcmp(argv[1], "noroot")) { assert(geteuid() == 0 && c.e == 0 && c.p == 0 && ctl(27, 0, 0) == 3); }
        else if (!strcmp(argv[1], "bound")) { assert(c.e == (ALL & ~BIT(13)) && c.p == c.e); }
        else if (!strcmp(argv[1], "inherit-bound")) { assert(c.p == ALL && ctl(23, 13, 0) == 0); }
        else if (!strcmp(argv[1], "nnp-id")) { assert(geteuid() == 1000 && getuid() == 1000 && c.p == ALL && c.e == ALL); }
        else if (!strcmp(argv[1], "fsuid")) { assert(setfsuid((uid_t)-1) == 0); }
        else if (!strcmp(argv[1], "sgid")) { assert(getegid() == 0); }
        else if (!strcmp(argv[1], "nnp-root")) { assert(geteuid() == 0 && c.e == 0 && c.p == 0); }
        else assert(0);
        return 0;
    }
    assert(get(0).e == ALL && get(0).p == ALL);
    int f=open("/secret", O_CREAT|O_WRONLY, 0000); assert(f >= 0); close(f); assert(chown("/secret", 1001, 1001) == 0);
    assert(chmod("/suid", 04755) == 0);
    assert(chown("/sgid", 0, 1001) == 0 && chmod("/sgid", 02745) == 0);
    isolated(v1_set); isolated(fsuid_exec); isolated(sgid_exec);
    isolated(fixup_and_inheritance); isolated(process_authority); isolated(inherit_bound_exec); isolated(nnp_id_exec);
    isolated(abi); isolated(enforcement); isolated(uid_transitions); isolated(locks); isolated(threads); isolated(cross_pid);
    isolated(nnp_exec); isolated(suid_exec); isolated(ambient_exec); isolated(noroot_exec); isolated(bound_exec); isolated(nnp_root_exec);
    puts("capabilities: ABI, enforcement, UID/exec, bounding/ambient/securebits, NNP, fork/thread/PID namespace: ok");
    return 0;
}
