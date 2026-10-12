// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s errno=%d\n",__LINE__,#x,errno); exit(1); } } while (0)
static void abi(void) {
    int fd=open("inventory-file",O_CREAT|O_TRUNC|O_RDWR,0600); CHECK(fd>=0);
    CHECK(write(fd,"abcdef",6)==6); CHECK(lseek(fd,3,SEEK_SET)==3);
    char a[4]={0},b[4]={0}; struct iovec iov[]={{a,2},{b,2}};
    CHECK(preadv(fd,iov,2,1)==4); CHECK(!memcmp(a,"bc",2)&&!memcmp(b,"de",2));
    CHECK(lseek(fd,0,SEEK_CUR)==3);
    memcpy(a,"XY",2); memcpy(b,"ZT",2);
    CHECK(pwritev(fd,iov,2,1)==4); CHECK(lseek(fd,0,SEEK_CUR)==3);
    char all[8]={0}; CHECK(pread(fd,all,6,0)==6); CHECK(!memcmp(all,"aXYZTf",6));
    // Full 64-bit pos_l on native arm64, including offsets above 4 GiB.
    CHECK(preadv(fd,iov,2,((off_t)1<<32)+1)==0);
    CHECK(preadv(fd,iov,2,-1)==-1&&errno==EINVAL);
    CHECK(preadv(fd,iov,0,0)==0);
    struct stat st; struct statx sx; CHECK(fstat(fd,&st)==0);
    CHECK(syscall(SYS_statx,fd,"",AT_EMPTY_PATH,STATX_BASIC_STATS,&sx)==0);
    CHECK((sx.stx_mask&STATX_BASIC_STATS)==STATX_BASIC_STATS);
    CHECK(sx.stx_ino==st.st_ino&&sx.stx_size==6&&sx.stx_mode==st.st_mode&&sx.stx_nlink==1);
    CHECK(sx.stx_uid==st.st_uid&&sx.stx_gid==st.st_gid);
    CHECK(sx.stx_atime.tv_sec==st.st_atim.tv_sec&&sx.stx_mtime.tv_nsec==st.st_mtim.tv_nsec);
    CHECK(symlink("inventory-file","inventory-link")==0);
    CHECK(syscall(SYS_statx,AT_FDCWD,"inventory-link",AT_SYMLINK_NOFOLLOW,STATX_TYPE,&sx)==0);
    CHECK(S_ISLNK(sx.stx_mode)&&sx.stx_size==14);
    CHECK(syscall(SYS_statx,AT_FDCWD,"inventory-link",0,STATX_TYPE,&sx)==0&&S_ISREG(sx.stx_mode));
    CHECK(syscall(SYS_statx,fd,"",AT_EMPTY_PATH,0x80000000U,&sx)==-1&&errno==EINVAL);
    CHECK(syscall(SYS_close_range,fd,fd,4)==0); CHECK(fcntl(fd,F_GETFD)&FD_CLOEXEC);
    CHECK(syscall(SYS_close_range,fd,fd,8)==-1&&errno==EINVAL);
    CHECK(syscall(SYS_close_range,fd+1,fd,0)==-1&&errno==EINVAL);
    CHECK(syscall(SYS_close_range,fd,~0U,0)==0); CHECK(fcntl(fd,F_GETFD)==-1&&errno==EBADF);
    unlink("inventory-link"); unlink("inventory-file"); puts("compatibility-abi-ok");
}
static void shared_fds(void) {
    for (int detach=1;detach>=0;detach--) {
        int fd=open("shared-fds-file",O_CREAT|O_RDONLY,0600); CHECK(fd>=0);
        pid_t child=syscall(SYS_clone,CLONE_FILES|SIGCHLD,0,0,0,0); CHECK(child>=0);
        if (!child) { _exit(syscall(SYS_close_range,fd,fd,detach?2:0)!=0); }
        int status; CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
        if (detach) { CHECK(fcntl(fd,F_GETFD)>=0); close(fd); }
        else CHECK(fcntl(fd,F_GETFD)==-1&&errno==EBADF);
    }
    unlink("shared-fds-file"); puts("shared-fds-ok");
}
static void log_test(void) {
    int fd=open("/dev/kmsg",O_RDWR|O_NONBLOCK); CHECK(fd>=0);
    CHECK(write(fd,"guest-log-marker",16)==16);
    pid_t child=fork(); CHECK(child>=0);
    if (!child) {
        CHECK(syscall(SYS_memfd_create,"unimplemented-probe",0)==-1&&errno==ENOSYS);
        CHECK(syscall(10000,1,2,3,4,5,6)==-1&&errno==ENOSYS);
#ifdef __aarch64__
        __asm__ volatile(".inst 0x2518e3e0" ::: "memory"); // PTRUE: unsupported SVE.
#else
        raise(SIGILL); // Host build is used only as an independent shared-fd oracle.
#endif
        _exit(1);
    }
    int status; CHECK(waitpid(child,&status,0)==child); CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==SIGILL);
    char data[8192]; int n=klogctl(3,data,sizeof(data)-1); CHECK(n>0); data[n]=0;
    CHECK(strstr(data,"guest-log-marker")&&strstr(data,"syscall=10000(unknown)")&&strstr(data,"0x2518e3e0"));
    CHECK(strstr(data,"pid=2")&&strstr(data,"syscall=279(memfd_create)"));
    // Linux READ_CLEAR changes snapshots, while /dev/kmsg retains its own cursor.
    CHECK(klogctl(4,data,sizeof(data))>0); CHECK(klogctl(3,data,sizeof(data))==0);
    CHECK(read(fd,data,sizeof(data))>0);
    CHECK(lseek(fd,0,SEEK_END)==0); CHECK(read(fd,data,sizeof(data))==-1&&errno==EAGAIN);
    int ready[2]; CHECK(pipe(ready)==0);
    child=fork(); CHECK(child>=0);
    if (!child) {
        close(ready[0]); int reader=open("/dev/kmsg",O_RDONLY); CHECK(reader>=0);
        CHECK(lseek(reader,0,SEEK_END)==0); CHECK(write(ready[1],"r",1)==1);
        int got=read(reader,data,sizeof(data)-1); CHECK(got>0); data[got]=0;
        CHECK(strstr(data,"follow-reader-marker")); _exit(0);
    }
    close(ready[1]); CHECK(read(ready[0],data,1)==1); close(ready[0]);
    CHECK(write(fd,"follow-reader-marker",20)==20);
    CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    CHECK(write(fd,"dmesg-after-exec-marker",23)==23); close(fd);
    child=fork(); CHECK(child>=0);
    if (!child) { execl("/bin/busybox","busybox","dmesg",(char *)0); _exit(2); }
    CHECK(waitpid(child,&status,0)==child); CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    puts("compatibility-log-ok");
}
int main(int argc,char **argv) { if (argc==2&&!strcmp(argv[1],"log")) log_test(); else if (argc==2&&!strcmp(argv[1],"shared-fds")) shared_fds(); else abi(); return 0; }
