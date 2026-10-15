// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>
#define DENIED(expr, code) do { errno=0; assert((expr)==-1 && errno==(code)); } while(0)
static void io(const char *base) {
    const char *names[]={"null","zero","full","random","urandom"};
    for(int i=0;i<5;i++) {
        char path[128],bytes[37]; snprintf(path,sizeof path,"%s/%s",base,names[i]);
        int fd=open(path,O_RDWR); assert(fd>=0);
        memset(bytes,1,sizeof bytes); ssize_t n=read(fd,bytes,sizeof bytes);
        assert(n==(i==0?0:(ssize_t)sizeof bytes));
        if(i==1 || i==2) for(size_t j=0;j<sizeof bytes;j++) assert(bytes[j]==0);
        if(i==2) { DENIED(write(fd,bytes,0),ENOSPC); DENIED(write(fd,bytes,sizeof bytes),ENOSPC); }
        else { assert(write(fd,bytes,0)==0); assert(write(fd,bytes,sizeof bytes)==sizeof bytes); }
        if(i<3) assert(lseek(fd,123,SEEK_SET)==0);
        close(fd);
    }
    puts("device I/O: null EOF, zero/full reads, full ENOSPC, random lengths and discarded writes: ok");
}
static void retained(void) {
    struct stat a,b;
    assert(stat("/nodes/alias",&a)==0 && a.st_rdev==makedev(1,5) && a.st_uid==123 && a.st_gid==456);
    assert(stat("/nodes/renamed",&b)==0 && a.st_ino==b.st_ino && a.st_nlink==2);
    int fd=open("/nodes/alias",O_RDWR); assert(fd>=0); char data[5]; assert(read(fd,data,5)==5); close(fd);
    DENIED(open("/nodes/unknown",O_RDWR),ENXIO);
    assert(stat("/nodes/unknown",&a)==0 && a.st_rdev==makedev(4095,1048575));
    DENIED(open("/nodes/block",O_RDONLY),ENXIO);
    assert(stat("/nodes/block",&a)==0 && S_ISBLK(a.st_mode) && a.st_rdev==makedev(1,3));
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"native")) { io("/dev"); return 0; }
    if(argc==2 && !strcmp(argv[1],"reopen")) { retained(); puts("device inode metadata and behavior survive disk reopen: ok"); return 0; }
    umask(0); assert(mkdir("/nodes",0777)==0);
    int dir=open("/nodes",O_RDONLY|O_DIRECTORY); assert(dir>=0);
    const char *names[]={"null","zero","full","random","urandom","tty","ptmx"};
    const unsigned numbers[]={0x103,0x105,0x107,0x108,0x109,0x500,0x502};
    for(size_t i=0;i<7;i++) {
        assert(mknodat(dir,names[i],S_IFCHR|0666,numbers[i])==0);
        struct stat st; assert(fstatat(dir,names[i],&st,0)==0 && S_ISCHR(st.st_mode) && st.st_rdev==numbers[i]);
        uint64_t how[3]={O_PATH|O_CLOEXEC,0,16};
        int meta=syscall(SYS_openat2,dir,names[i],how,sizeof how); assert(meta>=0);
        char via[64]; snprintf(via,sizeof via,"/proc/self/fd/%d",meta);
        int changed=chmod(via,0666);
        if(changed) { int error=errno; char target[256]={0}; readlink(via,target,sizeof target-1); fprintf(stderr,"chmod %s -> %s: errno=%d\n",via,target,error); }
        assert(changed==0 && chown(via,0,0)==0); close(meta);
    }
    io("/nodes");
    DENIED(mknodat(dir,"null",S_IFCHR|0666,0x103),EEXIST);
    DENIED(mknodat(dir,"invalid",S_IFDIR|0777,0),EINVAL);
    umask(0027); assert(mknodat(dir,"private",S_IFCHR|0666,0x105)==0); umask(0);
    struct stat st; assert(fstatat(dir,"private",&st,0)==0 && (st.st_mode&0777)==0640);
    assert(mknodat(dir,"unknown",S_IFCHR|0600,makedev(4095,1048575))==0);
    assert(mknodat(dir,"block",S_IFBLK|0600,makedev(1,3))==0);
    int fd=openat(dir,"unknown",O_PATH); assert(fd>=0 && fstat(fd,&st)==0 && st.st_rdev==makedev(4095,1048575)); close(fd);
    assert(link("/nodes/zero","/nodes/alias")==0 && rename("/nodes/zero","/nodes/renamed")==0);
    assert(chown("/nodes/renamed",123,456)==0); retained();
    DIR *list=opendir("/nodes"); assert(list); struct dirent *entry; int seen=0;
    while((entry=readdir(list))) { if(!strcmp(entry->d_name,"block")) { assert(entry->d_type==DT_BLK); seen++; }
        if(!strcmp(entry->d_name,"null")) { assert(entry->d_type==DT_CHR); seen++; } }
    assert(seen==2 && closedir(list)==0);
    // Reuse the existing guest PTY driver; this is not a private devpts instance.
    fd=openat(dir,"ptmx",O_RDWR|O_NOCTTY); assert(fd>=0); unsigned pty;
    assert(ioctl(fd,0x80045430,&pty)==0 && pty>0); close(fd);
    assert(mkdir("/bound",0755)==0 && mount("/nodes","/bound",NULL,MS_BIND,NULL)==0);
    fd=open("/bound/null",O_RDWR); assert(fd>=0);
    assert(mount(NULL,"/bound",NULL,MS_REMOUNT|MS_BIND|MS_NODEV,NULL)==0);
    DENIED(open("/bound/null",O_RDONLY),EACCES); assert(write(fd,"x",1)==1); close(fd);
    int metadata=open("/bound/null",O_PATH); assert(metadata>=0); close(metadata);
    assert(mount(NULL,"/bound",NULL,MS_REMOUNT|MS_BIND|MS_RDONLY,NULL)==0);
    DENIED(mknod("/bound/denied",S_IFCHR|0600,makedev(1,3)),EROFS);
    assert(umount("/bound")==0);
    pid_t child=fork(); assert(child>=0);
    if(!child) {
        struct { uint32_t version; int pid; } h={0x20080522,0};
        struct { uint32_t e,p,i; } caps[2]; assert(syscall(SYS_capget,&h,caps)==0);
        caps[0].e &= ~(1U<<27); assert(syscall(SYS_capset,&h,caps)==0);
        DENIED(mknodat(dir,"denied",S_IFCHR|0666,0x103),EPERM);
        DENIED(mknodat(dir,"denied-block",S_IFBLK|0666,0xfd01),EPERM);
        assert(mknodat(dir,"fifo",S_IFIFO|0600,123)==0 && mknodat(dir,"regular",S_IFREG|0600,123)==0);
        assert(setgid(1000)==0 && setuid(1000)==0); DENIED(open("/nodes/private",O_RDONLY),EACCES);
        fd=open("/nodes/null",O_RDWR); assert(fd>=0 && write(fd,"x",1)==1); _exit(0);
    }
    int status; assert(waitpid(child,&status,0)==child && status==0); close(dir);
    puts("device nodes: numbers, permissions, hard links, directory types, PTY reuse, nodev and readonly: ok");
    return 0;
}
