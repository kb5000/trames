// SPDX-License-Identifier: GPL-3.0-or-later
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check %d: %s errno=%d\n",__LINE__,#x,errno); exit(1); } } while (0)
int main(int argc,char **argv) {
 CHECK(argc==2);
 if (!strcmp(argv[1],"dynamic")) {
  CHECK(getauxval(AT_BASE)!=0 && getauxval(AT_ENTRY)!=0 && getauxval(AT_BASE)!=getauxval(AT_ENTRY));
  void *h=dlopen("/test/libm3probe.so",RTLD_NOW); CHECK(h);
  long (*fn)(long)=dlsym(h,"probe"); CHECK(fn && fn(5)==12);
  pid_t pid=fork();CHECK(pid>=0);
  if (!pid) { CHECK(fn(8)==20); _exit(0); }
  int status; CHECK(waitpid(pid,&status,0)==pid && status==0 && fn(3)==15);
  CHECK(dlclose(h)==0); puts("dynamic dlopen TLS fork ok");
 } else if (!strcmp(argv[1],"mapping")) {
  int fd=open("mapped",O_RDWR|O_CREAT|O_TRUNC,0600);CHECK(fd>=0);
  char input[8192];for (unsigned i=0;i<sizeof input;i++)input[i]=(char)(i*19+7);
  CHECK(write(fd,input,sizeof input)==sizeof input && lseek(fd,17,SEEK_SET)==17);
  char *p=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,4096);CHECK(p!=MAP_FAILED);
  CHECK(!memcmp(p,input+4096,4096) && lseek(fd,0,SEEK_CUR)==17);
  p[0]=99;char b;CHECK(pread(fd,&b,1,4096)==1 && b==input[4096]);
  errno=0;CHECK(mmap(p,4096,PROT_READ,MAP_PRIVATE|MAP_FIXED,-1,0)==MAP_FAILED && errno==EBADF && p[0]==99);
  CHECK(mprotect(p,4096,PROT_READ)==0 && munmap(p,4096)==0 && close(fd)==0);
  puts("private mmap offset protection preflight ok");
 } else if (!strcmp(argv[1],"links")) {
  CHECK(symlink("../etc/alpine-release","alias")==0);
  char b[3]={1,2,3};CHECK(readlink("alias",b,2)==2 && b[0]=='.' && b[1]=='.' && b[2]==3);
  errno=0;CHECK(open("alias",O_RDONLY|O_NOFOLLOW)==-1 && errno==ELOOP);
  CHECK(unlink("alias")==0 && open("/etc/alpine-release",O_RDONLY)>=0);
  puts("symlink nofollow readlink unlink ok");
 } else if (!strcmp(argv[1],"locks")) {
  int a=open("lock",O_RDWR|O_CREAT,0600),b=open("lock",O_RDWR);CHECK(a>=0 && b>=0);
  CHECK(flock(a,LOCK_EX)==0);errno=0;CHECK(flock(b,LOCK_EX|LOCK_NB)==-1 && errno==EWOULDBLOCK);
  pid_t pid=fork();CHECK(pid>=0);
  if (!pid) { close(a);CHECK(flock(b,LOCK_EX)==0 && flock(b,LOCK_UN)==0);_exit(0); }
  struct timespec delay={0,2000000};CHECK(nanosleep(&delay,0)==0 && flock(a,LOCK_UN)==0);
  int status;CHECK(waitpid(pid,&status,0)==pid && status==0);
  unsigned char x[32],y[32];CHECK(getrandom(x,32,0)==32 && getrandom(y,32,0)==32 && memcmp(x,y,32));
  puts("flock wait fork entropy ok");
 } else return 2;
 return 0;
}
