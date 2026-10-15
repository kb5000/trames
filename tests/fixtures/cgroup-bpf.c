// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#define CG "/sys/fs/cgroup"
#define DENIED(expr, code) do { errno=0; assert((expr)==-1 && errno==(code)); } while (0)
struct insn { uint8_t code, regs; int16_t off; int32_t imm; };
_Static_assert(sizeof(struct insn)==8,"BPF instruction ABI");
struct attr { unsigned char b[152]; };
static void u32(struct attr *a, unsigned o, uint32_t x) { memcpy(a->b+o,&x,4); }
static void u64(struct attr *a, unsigned o, uint64_t x) { memcpy(a->b+o,&x,8); }
static uint32_t get32(const struct attr *a, unsigned o) { uint32_t x; memcpy(&x,a->b+o,4); return x; }
static int bpf(unsigned cmd,struct attr *a) { return syscall(SYS_bpf,cmd,a,sizeof *a); }
static int load(const struct insn *insns, unsigned n) {
 struct attr a={0}; u32(&a,0,15); u32(&a,4,n); u64(&a,8,(uintptr_t)insns); u64(&a,16,(uintptr_t)"GPL");
 return bpf(5,&a);
}
static int attach(int target,int program,unsigned flags,int replace) {
 struct attr a={0}; u32(&a,0,target);u32(&a,4,program);u32(&a,8,6);u32(&a,12,flags);u32(&a,16,replace);return bpf(8,&a);
}
static int detach(int target,int program) {
 struct attr a={0};u32(&a,0,target);u32(&a,4,program);u32(&a,8,6);return bpf(9,&a);
}
static int getfd(unsigned id) { struct attr a={0};u32(&a,0,id);return bpf(13,&a); }
static unsigned query(int target,int effective,uint32_t *ids,unsigned count,int expected) {
 struct attr a={0};u32(&a,0,target);u32(&a,4,6);u32(&a,8,effective);u64(&a,16,(uintptr_t)ids);u32(&a,24,count);
 errno=0;int r=bpf(16,&a);if(expected)assert(r==-1 && errno==expected);else assert(r==0);
 return get32(&a,24);
}
static void move(const char *path) { int f=open(path,O_WRONLY);assert(f>=0);assert(write(f,"0",1)==1);close(f); }
static void wait_ok(pid_t p) { int s;assert(waitpid(p,&s,0)==p && WIFEXITED(s) && WEXITSTATUS(s)==0); }
int main(void) {
 const struct insn allow[]={{0xb7,0,0,1},{0x95,0,0,0}};
 const struct insn deny[]={{0xb7,0,0,0},{0x95,0,0,0}};
 const struct insn invalid[]={{0x61,0x10,12,0},{0x95,0,0,0}};
 DENIED(load(invalid,2),EINVAL);DENIED(load(allow,0),EINVAL);DENIED(load(allow,4097),E2BIG);
 DENIED(load((void*)1,2),EFAULT);
 struct attr a={0};u32(&a,0,15);u32(&a,4,2);u64(&a,8,(uintptr_t)allow);u64(&a,16,(uintptr_t)"GPL");
 u32(&a,24,1);u32(&a,28,64);u64(&a,32,1);DENIED(bpf(5,&a),EFAULT);
 u32(&a,24,0);u32(&a,28,0);u64(&a,32,0);a.b[151]=1;DENIED(bpf(5,&a),E2BIG);
 int yes=load(allow,2),no=load(deny,2);assert(yes>=0 && no>=0);assert(fcntl(yes,F_GETFD)&FD_CLOEXEC);
 assert(mkdir(CG "/bpf",0755)==0 && mkdir(CG "/bpf/leaf",0755)==0);
 int parent=open(CG "/bpf",O_RDONLY|O_DIRECTORY),child=open(CG "/bpf/leaf",O_PATH);assert(parent>=0 && child>=0);
 assert(attach(parent,no,2,0)==0);DENIED(attach(parent,no,2,0),EEXIST);
 uint32_t ids[8];assert(query(parent,0,NULL,0,ENOSPC)==1);assert(query(parent,0,ids,8,0)==1);unsigned denyid=ids[0];
 close(no);no=getfd(denyid);assert(no>=0);
 assert(query(child,1,ids,8,0)==1 && ids[0]==denyid);assert(attach(child,yes,2,0)==0);
 int existing=open("/dev/null",O_RDWR);assert(existing>=0);
 int master=open("/dev/ptmx",O_RDWR);assert(master>=0);int unlocked=0;assert(ioctl(master,0x40045431,&unlocked)==0);
 move(CG "/bpf/leaf/cgroup.procs");
 DENIED(open("/dev/null",O_RDONLY),EPERM);DENIED(open("/dev/ptmx",O_RDWR),EPERM);DENIED(ioctl(master,0x5441,O_RDWR),EPERM);
 int path=open("/dev/null",O_PATH);assert(path>=0);char proc[80];snprintf(proc,sizeof proc,"/proc/self/fd/%d",path);DENIED(open(proc,O_RDONLY),EPERM);close(path);
 DENIED(mknod("/bpf-null",S_IFCHR|0600,0x103),EPERM);
 assert(write(existing,"x",1)==1); // existing FD remains usable
 pid_t p=fork();assert(p>=0);if(!p){DENIED(open("/dev/zero",O_RDONLY),EPERM);_exit(0);}wait_ok(p);
 assert(attach(parent,yes,6,no)==0);int fd=open("/dev/null",O_RDWR);assert(fd>=0);close(fd);
 close(no);DENIED(getfd(denyid),ENOENT);
 assert(detach(parent,yes)==0 && detach(child,yes)==0);assert(query(child,1,ids,8,0)==0);
 // A malformed query output must not change the caller's count.
 memset(&a,0,sizeof a);u32(&a,0,parent);u32(&a,4,6);u64(&a,16,1);u32(&a,24,8);
 assert(attach(parent,yes,2,0)==0);DENIED(bpf(16,&a),EFAULT);assert(get32(&a,24)==8);
 assert(detach(parent,yes)==0);
 move(CG "/cgroup.procs");
 fd=open(CG "/bpf/io.max",O_RDWR);assert(fd>=0);char b;assert(read(fd,&b,1)==0);DENIED(write(fd,"253:1 rbps=1",12),EOPNOTSUPP);close(fd);
 p=fork();assert(p>=0);if(!p){assert(setuid(1234)==0);DENIED(load(allow,2),EPERM);_exit(0);}wait_ok(p);
 close(master);close(existing);close(yes);close(parent);close(child);
 assert(rmdir(CG "/bpf/leaf")==0 && rmdir(CG "/bpf")==0);
 puts("cgroup BPF: verified device policy, attachment lifetime, replacement, inheritance, fault and permission checks: ok");
 return 0;
}
