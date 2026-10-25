// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s errno=%d\n",__LINE__,#x,errno);exit(1);}} while(0)
#define P 4096
static volatile void *volatile expected_bus;
static void bus_handler(int signal,siginfo_t *info,void *context){(void)context;_exit(!(signal==SIGBUS&&info->si_code==BUS_ADRERR&&info->si_addr==expected_bus));}
static void child_status(pid_t pid,int sig){int status;CHECK(waitpid(pid,&status,0)==pid);if(sig)CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==sig);else CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);}
static void reserved_fault(int signal,siginfo_t *info,void *context){(void)context;_exit(!(signal==SIGSEGV&&info->si_code==SEGV_ACCERR&&info->si_addr==expected_bus));}
static void reservations(void){
 size_t size=(size_t)4*1024*1024*1024;
 unsigned char *p=mmap(NULL,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(p!=MAP_FAILED);
 CHECK(mmap(p+P,P,PROT_READ,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==MAP_FAILED&&errno==EEXIST);
 unsigned char state=1;CHECK(mincore(p,P,&state)==0&&state==0);
 CHECK(madvise(p,size,MADV_NORMAL)==0&&msync(p,size,MS_SYNC)==0);
 pid_t child=fork();CHECK(child>=0);if(!child){struct sigaction sa={.sa_sigaction=reserved_fault,.sa_flags=SA_SIGINFO};CHECK(sigaction(SIGSEGV,&sa,NULL)==0);expected_bus=p;unsigned char byte=*(volatile unsigned char *)p;(void)byte;_exit(1);}child_status(child,0);
 CHECK(mprotect(p+P,P,PROT_READ|PROT_WRITE)==0);p[P]=42;
 CHECK(mincore(p+P,P,&state)==0&&(state&1));
 CHECK(mmap(p+2*P,P,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==p+2*P);p[2*P]=17;
 CHECK(munmap(p+3*P,P)==0);
 CHECK(mprotect(p+2*P,2*P,PROT_READ)==-1&&errno==ENOMEM);
 // Linux may change the mapped prefix before failing at a hole.
 CHECK(mprotect(p+2*P,P,PROT_READ|PROT_WRITE)==0);p[2*P]=18;CHECK(p[P]==42);
 CHECK(munmap(p,size)==0);
 p=mmap(NULL,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(p!=MAP_FAILED);
 void *moved=mremap(p,size,size+P,MREMAP_MAYMOVE);CHECK(moved!=MAP_FAILED);
 CHECK(mprotect(moved,P,PROT_READ|PROT_WRITE)==0&&*(unsigned char *)moved==0);
 CHECK(munmap(moved,size+P)==0);
}
static void mappings(void){
 reservations();
 int fd=open("mapped-file",O_CREAT|O_TRUNC|O_RDWR,0600);CHECK(fd>=0&&ftruncate(fd,P*2)==0);
 volatile unsigned char *a=mmap(0,P*3,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(a!=MAP_FAILED);
 volatile unsigned char *b=mmap(0,P*2,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(b!=MAP_FAILED);
 a[5]=17;CHECK(b[5]==17);unsigned char c;CHECK(pread(fd,&c,1,5)==1&&c==17);
 c=23;CHECK(pwrite(fd,&c,1,6)==1&&a[6]==23&&b[6]==23);
 volatile unsigned char *private=mmap(0,P,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);CHECK(private!=MAP_FAILED);private[5]=99;CHECK(a[5]==17);
 CHECK(madvise((void *)private,P,MADV_DONTNEED)==0&&private[5]==17);private[5]=17;
 pid_t child=fork();CHECK(child>=0);if(!child){a[5]=31;private[5]=41;_exit(0);}child_status(child,0);CHECK(a[5]==31&&private[5]==17);
 volatile int *anon=mmap(0,P,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);CHECK(anon!=MAP_FAILED);*anon=1;
 child=fork();CHECK(child>=0);if(!child){*anon=7;_exit(0);}child_status(child,0);CHECK(*anon==7);CHECK(munmap((void *)anon,P)==0);
 CHECK(msync((void *)a,P*2,MS_SYNC)==0);CHECK(msync((void *)a,P,MS_ASYNC|MS_SYNC)==-1&&errno==EINVAL);
 child=fork();CHECK(child>=0);if(!child){c=a[P*2];(void)c;_exit(1);}child_status(child,SIGBUS);
 CHECK(ftruncate(fd,P+7)==0);CHECK(a[P+6]==0);a[P+10]=88;CHECK(b[P+10]==88);
 CHECK(msync((void *)a,P*2,MS_SYNC)==0);struct stat st;CHECK(fstat(fd,&st)==0&&st.st_size==P+7);
 child=fork();CHECK(child>=0);if(!child){struct sigaction sa={0};sa.sa_sigaction=bus_handler;sa.sa_flags=SA_SIGINFO;CHECK(sigaction(SIGBUS,&sa,0)==0);expected_bus=(void *)(a+P*2);c=a[P*2];(void)c;_exit(1);}child_status(child,0);
 CHECK(ftruncate(fd,1)==0);child=fork();CHECK(child>=0);if(!child){c=a[P];(void)c;_exit(1);}child_status(child,SIGBUS);
 CHECK(ftruncate(fd,P*2)==0&&a[P]==0);CHECK(close(fd)==0&&unlink("mapped-file")==0);a[5]=45;CHECK(b[5]==45);
 CHECK(munmap((void *)private,P)==0&&munmap((void *)a,P*3)==0&&munmap((void *)b,P*2)==0);
 // Own the guard page too: MAP_FIXED below must not overwrite an unrelated host/guest mapping.
 unsigned char *region=mmap(0,P*3,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(region!=MAP_FAILED);region[0]=12;*((volatile unsigned char *)region+P)=0;
 unsigned char resident[2];CHECK(mincore(region,P*2,resident)==0&&(resident[0]&1)&&(resident[1]&1));
 CHECK(mmap(region+P*2,P,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==region+P*2);
 unsigned char *grown=mremap(region,P*2,P*4,MREMAP_MAYMOVE);CHECK(grown!=MAP_FAILED&&grown[0]==12&&grown[P*3]==0);
 grown[8]=13;CHECK(madvise(grown,P,MADV_DONTNEED)==0&&grown[8]==0);CHECK(munmap(grown,P*4)==0&&munmap(region+P*2,P)==0);
}
static void memfds(void){
 int fd=syscall(SYS_memfd_create,"mapping-test",3);CHECK(fd>=0&&ftruncate(fd,P)==0);
 CHECK((fcntl(fd,F_GETFD)&FD_CLOEXEC)!=0&&fcntl(fd,F_GET_SEALS)==0);
 volatile uint32_t *rw=mmap(0,P,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(rw!=MAP_FAILED);
 CHECK(fcntl(fd,F_ADD_SEALS,F_SEAL_WRITE)==-1&&errno==EBUSY);
 int ro=open("/proc/self/fd/0",O_RDONLY);if(ro>=0)close(ro);
 rw[0]=0xd2800020;rw[1]=0xd65f03c0; // mov x0,#1; ret
 void *rx=mmap(0,P,PROT_READ|PROT_EXEC,MAP_SHARED,fd,0);CHECK(rx!=MAP_FAILED);
 __builtin___clear_cache(rx,(char *)rx+8);unsigned long (*fn)(void)=(unsigned long(*)(void))rx;CHECK(fn()==1);
 rw[0]=0xd2800040;__builtin___clear_cache(rx,(char *)rx+8);CHECK(fn()==2);
 uint32_t word=0xd2800060;CHECK(pwrite(fd,&word,4,0)==4);__builtin___clear_cache(rx,(char *)rx+8);CHECK(fn()==3);
 CHECK(munmap((void *)rw,P)==0&&munmap(rx,P)==0);CHECK(fcntl(fd,F_ADD_SEALS,F_SEAL_SHRINK|F_SEAL_GROW|F_SEAL_WRITE)==0);
 CHECK(pwrite(fd,"x",1,0)==-1&&errno==EPERM);CHECK(ftruncate(fd,1)==-1&&errno==EPERM);
 CHECK(fcntl(fd,F_ADD_SEALS,F_SEAL_SEAL)==0&&fcntl(fd,F_GET_SEALS)==15);
 CHECK(fcntl(fd,F_ADD_SEALS,0)==-1&&errno==EPERM);close(fd);
}
static void mapping_ofds(void){
 for(int shared=0;shared<2;shared++){
  int fd=open("mapped-ofd",O_CREAT|O_TRUNC|O_RDWR,0600);CHECK(fd>=0&&ftruncate(fd,P*4)==0);
  int other=open("mapped-ofd",O_RDWR);CHECK(other>=0&&flock(fd,LOCK_EX|LOCK_NB)==0);
  int notify=inotify_init1(IN_NONBLOCK|IN_CLOEXEC);CHECK(notify>=0);
  int watch=inotify_add_watch(notify,"mapped-ofd",IN_CLOSE_WRITE);CHECK(watch>=0);
  unsigned char *p=mmap(NULL,P*2,PROT_READ|PROT_WRITE,shared?MAP_SHARED:MAP_PRIVATE,fd,0);CHECK(p!=MAP_FAILED);
  p[0]=7;CHECK(close(fd)==0);
  CHECK(flock(other,LOCK_EX|LOCK_NB)==-1&&errno==EWOULDBLOCK);
  char events[128];CHECK(read(notify,events,sizeof events)==-1&&errno==EAGAIN);
  CHECK(madvise(p,P*2,MADV_DONTNEED)==0&&p[0]==(shared?7:0));
  p=mremap(p,P*2,P*3,MREMAP_MAYMOVE);CHECK(p!=MAP_FAILED&&p[P*2]==0);
  CHECK(mprotect(p,P*3,PROT_READ)==0);
  CHECK(mmap(p,P,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==p);
  CHECK(flock(other,LOCK_EX|LOCK_NB)==-1&&errno==EWOULDBLOCK);
  CHECK(read(notify,events,sizeof events)==-1&&errno==EAGAIN);
  CHECK(munmap(p+P,P*2)==0&&flock(other,LOCK_EX|LOCK_NB)==0);
  CHECK(read(notify,events,sizeof events)==(ssize_t)sizeof(struct inotify_event));
  struct inotify_event event;memcpy(&event,events,sizeof event);CHECK(event.wd==watch&&event.mask==IN_CLOSE_WRITE);
  CHECK(read(notify,events,sizeof events)==-1&&errno==EAGAIN);
  CHECK(munmap(p,P)==0&&close(notify)==0&&close(other)==0&&unlink("mapped-ofd")==0);
 }
}
static void files(void){
 int a=open("source-file",O_CREAT|O_TRUNC|O_RDWR,0600);int b=open("dest-file",O_CREAT|O_TRUNC|O_RDWR,0600);CHECK(a>=0&&b>=0);
 CHECK(write(a,"abcdef",6)==6&&lseek(a,0,SEEK_SET)==0);
 off_t off=1;CHECK(sendfile(b,a,&off,3)==3&&off==4&&lseek(a,0,SEEK_CUR)==0);
 off_t from=3,to=4;CHECK(syscall(SYS_copy_file_range,a,&from,b,&to,3,0)==3&&from==6&&to==7);
 char bytes[8]={0};CHECK(pread(b,bytes,7,0)==7&&!memcmp(bytes,"bcd\0def",7));
 struct iovec vec={bytes,2};CHECK(syscall(SYS_preadv2,a,&vec,1,1,0,0)==2&&!memcmp(bytes,"bc",2));
 memcpy(bytes,"XY",2);CHECK(syscall(SYS_pwritev2,b,&vec,1,0,0,0)==2);
 CHECK(syscall(SYS_syncfs,b)==0&&syscall(SYS_readahead,a,0,6)==0&&syscall(SYS_fadvise64,a,0,0,0)==0);
 CHECK(syscall(SYS_fchmodat2,AT_FDCWD,"dest-file",0640,0)==0);struct stat st;CHECK(fstat(b,&st)==0&&(st.st_mode&0777)==0640);
 uint64_t how[3]={O_RDONLY,0,0};int newer=syscall(SYS_openat2,AT_FDCWD,"dest-file",how,sizeof(how));CHECK(newer>=0);close(newer);
 close(a);close(b);unlink("source-file");unlink("dest-file");

}
static void scheduler(void){
 unsigned cpu=99,node=99;CHECK(syscall(SYS_getcpu,&cpu,&node,0)==0); // Native Linux may use another CPU; virtual profile reports zero.
 struct sched_param param;CHECK(syscall(SYS_sched_getparam,0,&param)==0&&param.sched_priority==0&&syscall(SYS_sched_getscheduler,0)==SCHED_OTHER);
 CHECK(syscall(SYS_sched_setparam,0,&param)==0&&syscall(SYS_sched_setscheduler,0,SCHED_OTHER,&param)==0);
 cpu_set_t mask;CPU_ZERO(&mask);CHECK(sched_getaffinity(0,sizeof(mask),&mask)==0);CHECK(sched_setaffinity(0,sizeof(mask),&mask)==0);
 CHECK(sched_get_priority_max(SCHED_FIFO)==99&&sched_get_priority_min(SCHED_FIFO)==1);
}
static void persistence(int write_it){
 int fd=open("mmap-persist",write_it?(O_CREAT|O_TRUNC|O_RDWR):O_RDONLY,0600);CHECK(fd>=0);
 if(write_it){CHECK(ftruncate(fd,P)==0);char *p=mmap(0,P,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(p!=MAP_FAILED);memcpy(p,"persist",7);CHECK(msync(p,P,MS_SYNC)==0);close(fd);CHECK(munmap(p,P)==0);}
 else {char out[7];CHECK(read(fd,out,7)==7&&!memcmp(out,"persist",7));close(fd);CHECK(unlink("mmap-persist")==0);}
}
int main(int argc,char **argv){if(argc==2&&!strcmp(argv[1],"scheduler"))scheduler();else if(argc==2&&!strcmp(argv[1],"persist-write"))persistence(1);else if(argc==2&&!strcmp(argv[1],"persist-read"))persistence(0);else {mappings();mapping_ofds();memfds();files();}puts("mmap-coverage-ok");return 0;}
