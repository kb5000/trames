// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
struct how { uint64_t flags,mode,resolve; };
static int call(int base,const char *path,uint64_t flags,uint64_t resolve) {
    struct how h={flags,flags&O_CREAT?0600:0,resolve}; return syscall(SYS_openat2,base,path,&h,sizeof h);
}
static void result(const char *label,int fd,int error,int byte) {
    if(error) { if(fd!=-1 || errno!=error) fprintf(stderr,"%s: fd=%d errno=%d expected=%d\n",label,fd,errno,error); assert(fd==-1 && errno==error); }
    else { if(fd<0) fprintf(stderr,"%s: errno=%d\n",label,errno); assert(fd>=0); if(byte) { char b; assert(read(fd,&b,1)==1 && b==byte); } close(fd); }
    printf("%s: ok\n",label);
}
static void file(const char *path,char byte) { int f=open(path,O_CREAT|O_WRONLY,0600); assert(f>=0 && write(f,&byte,1)==1); close(f); }
int main(int argc,char **argv) {
    assert(mkdir("scope",0700)==0 && mkdir("scope/sub",0700)==0);
    file("outside",'O'); file("scope/file",'I'); file("scope/proc",'L');
    assert(symlink("file","scope/relative")==0 && symlink("/file","scope/absolute")==0);
    assert(symlink("../outside","scope/escape")==0 && symlink("..","scope/up")==0);
    int base=open("scope",O_PATH|O_DIRECTORY); assert(base>=0);
    result("beneath relative",call(base,"relative",O_RDONLY,8),0,'I');
    result("beneath dotdot allowed inside",call(base,"sub/../file",O_RDONLY,8),0,'I');
    result("beneath dotdot escape",call(base,"../outside",O_RDONLY,8),EXDEV,0);
    result("beneath relative link escape",call(base,"escape",O_RDONLY,8),EXDEV,0);
    result("beneath absolute path",call(base,"/file",O_RDONLY,8),EXDEV,0);
    result("beneath absolute link",call(base,"absolute",O_RDONLY,8),EXDEV,0);
    result("in-root absolute path",call(base,"/file",O_RDONLY,16),0,'I');
    result("in-root absolute link",call(base,"absolute",O_RDONLY,16),0,'I');
    result("in-root dotdot clamp",call(base,"../../file",O_RDONLY,16),0,'I');
    result("in-root symlink clamp",call(base,"up/file",O_RDONLY,16),0,'I');
    result("in-root root differs from process root",call(base,"/proc",O_RDONLY,16),0,'L');
    result("in-root create",call(base,"/created",O_RDWR|O_CREAT,16),0,0);
    assert(faccessat(base,"created",F_OK,0)==0 && access("/created",F_OK)==-1);
    result("incompatible containment",call(base,"file",O_RDONLY,24),EINVAL,0);
    result("unknown resolve flag",call(base,"file",O_RDONLY,64),EINVAL,0);
    result("no-symlinks denies normal link",call(base,"relative",O_RDONLY,4),ELOOP,0);
    result("no-magic allows normal link",call(base,"relative",O_RDONLY,2),0,'I');
    result("no-symlinks allows link metadata",call(base,"relative",O_PATH|O_NOFOLLOW,4),0,0);
    result("nofollow non-path still ELOOP",call(base,"relative",O_RDONLY|O_NOFOLLOW,4),ELOOP,0);
    result("no-magic rejects proc exe",call(AT_FDCWD,"/proc/self/exe",O_RDONLY,2),ELOOP,0);
    result("no-symlinks rejects proc self",call(AT_FDCWD,"/proc/self/status",O_RDONLY,4),ELOOP,0);
    result("no-magic allows proc self",call(AT_FDCWD,"/proc/self/status",O_RDONLY,2),0,0);
    char magic[80]; snprintf(magic,sizeof magic,"/proc/self/fd/%d/file",base);
    result("magic directory rejected",call(AT_FDCWD,magic,O_RDONLY,2),ELOOP,0);
    result("no-xdev proc mount",call(AT_FDCWD,"/proc/self/status",O_RDONLY,1),EXDEV,0);
    int proc=open("/proc",O_PATH|O_DIRECTORY); assert(proc>=0);
    result("no-xdev within proc",call(proc,"self/status",O_RDONLY,1),0,0);
    result("no-xdev magic target",call(proc,"self/exe",O_RDONLY,1),EXDEV,0);
    result("no-magic final metadata",call(proc,"self/exe",O_PATH|O_NOFOLLOW,2),0,0);
    result("missing magic target",call(proc,"self/fd/9999",O_RDONLY,2),ENOENT,0);
    result("O_PATH rejects create",call(base,"must-not-exist",O_PATH|O_CREAT,16),EINVAL,0);
    assert(faccessat(base,"must-not-exist",F_OK,0)==-1);
    result("regular file as dirfd",call(open("outside",O_RDONLY),"file",O_RDONLY,16),ENOTDIR,0);
    file("scope/meta",'M'); int meta=open("scope/meta",O_PATH); assert(meta>=0);
    assert(unlink("scope/meta")==0); file("scope/meta",'N');
    snprintf(magic,sizeof magic,"/proc/self/fd/%d",meta);
    assert(chmod(magic,0400)==0 && chown(magic,getuid(),getgid())==0);
    struct stat old,new; assert(fstat(meta,&old)==0 && stat("scope/meta",&new)==0);
    assert((old.st_mode&0777)==0400 && (new.st_mode&0777)==0600 && old.st_ino!=new.st_ino);
    close(meta); puts("proc FD metadata retains deleted inode identity: ok");
    close(proc);
    if(argc>1 && !strcmp(argv[1],"mounts")) {
        assert(mkdir("scope/mounted",0700)==0 && mount("none","scope/mounted","tmpfs",0,NULL)==0);
        result("no-xdev tmpfs",call(base,"mounted",O_RDONLY|O_DIRECTORY,1),EXDEV,0);
        assert(mkdir("bound",0700)==0 && mount("scope/sub","bound",NULL,MS_BIND,NULL)==0);
        result("no-xdev bind mount",call(AT_FDCWD,"bound",O_RDONLY|O_DIRECTORY,1),EXDEV,0);
        int fd=open("bound",O_PATH|O_DIRECTORY); assert(fd>=0);
        result("no-xdev escape bind",call(fd,"..",O_RDONLY|O_DIRECTORY,1),EXDEV,0);
        close(fd); assert(umount("bound")==0 && umount("scope/mounted")==0);
        // RESOLVE_CACHED always requests a retry; there is no I/O-free guarantee.
        result("cached lookup requests retry",call(base,"file",O_RDONLY,32),EAGAIN,0);
    }
    close(base); return 0;
}
