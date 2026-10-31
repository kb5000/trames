// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"line %d: %s: %s\n",__LINE__,#x,strerror(errno));exit(1);} } while(0)
static int apply(int fd,int cmd,int kind,long start,long len,int pid) {
 struct flock lock={.l_type=kind,.l_whence=SEEK_SET,.l_start=start,.l_len=len,.l_pid=pid};
 return fcntl(fd,cmd,&lock);
}
static const char *path="/tmp/trames-ofd-fixture";
static int waitfd,notify;
static void *waiter(void *unused){(void)unused;CHECK(write(notify,"t",1)==1);int result=apply(waitfd,F_OFD_SETLKW,F_WRLCK,0,0,0);CHECK(result==0);return NULL;}
int main(void){
 alarm(20);CHECK(mkdir("/tmp",01777)==0||errno==EEXIST);unlink(path);int fd=open(path,O_CREAT|O_RDWR|O_EXCL,0600);CHECK(fd>=0);int alias=dup(fd),other=open(path,O_RDWR);CHECK(alias>=0&&other>=0);
 CHECK(apply(fd,F_OFD_SETLK,F_WRLCK,4,8,0)==0);
 struct flock q={.l_type=F_WRLCK,.l_whence=SEEK_SET,.l_start=4,.l_len=8};
 CHECK(fcntl(other,F_GETLK,&q)==0&&q.l_type==F_WRLCK&&q.l_pid==-1);
 CHECK(apply(other,F_OFD_SETLK,F_WRLCK,4,8,0)<0&&errno==EAGAIN);
 CHECK(apply(fd,F_SETLK,F_WRLCK,4,8,0)<0&&errno==EAGAIN);
 CHECK(apply(fd,F_OFD_SETLK,F_WRLCK,4,8,1)<0&&errno==EINVAL);
 CHECK(apply(alias,F_OFD_SETLK,F_UNLCK,6,2,0)==0);
 CHECK(apply(other,F_OFD_SETLK,F_WRLCK,6,2,0)==0);
 CHECK(close(fd)==0);CHECK(apply(other,F_OFD_SETLK,F_WRLCK,4,2,0)<0&&errno==EAGAIN);
 CHECK(close(alias)==0);CHECK(apply(other,F_OFD_SETLK,F_WRLCK,4,2,0)==0);close(other);
 puts("ofd-ownership-ranges-posix: ok");
 // Parent holds one OFD; child blocks on a separate one. Closing/reusing its
 // numeric fd in another thread must not change the already accepted request.
 int owner=open(path,O_RDWR);CHECK(owner>=0);CHECK(apply(owner,F_OFD_SETLK,F_WRLCK,0,0,0)==0);
 int ready[2],release[2];CHECK(pipe(ready)==0&&pipe(release)==0);
 pid_t child=fork();CHECK(child>=0);
 if(child==0){close(owner);close(ready[0]);close(release[1]);waitfd=open(path,O_RDWR);CHECK(waitfd>=0);notify=ready[1];pthread_t thread;CHECK(pthread_create(&thread,NULL,waiter,NULL)==0);char b;CHECK(read(release[0],&b,1)==1);usleep(50000);CHECK(close(waitfd)==0);int replacement=open("/dev/null",O_RDWR);CHECK(replacement==waitfd);CHECK(write(notify,"c",1)==1);CHECK(pthread_join(thread,NULL)==0);_exit(0);}
 close(ready[1]);close(release[0]);char b;CHECK(read(ready[0],&b,1)==1&&b=='t');CHECK(write(release[1],"r",1)==1);CHECK(read(ready[0],&b,1)==1&&b=='c');CHECK(apply(owner,F_OFD_SETLK,F_UNLCK,0,0,0)==0);int status;CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);close(owner);close(ready[0]);close(release[1]);
 puts("ofd-blocking-close-reuse: ok");
 struct rlimit old,now;CHECK(getrlimit(RLIMIT_CORE,&old)==0);now=old;now.rlim_cur=old.rlim_max;CHECK(setrlimit(RLIMIT_CORE,&now)==0);CHECK(getrlimit(RLIMIT_CORE,&now)==0&&now.rlim_cur==old.rlim_max);CHECK(setrlimit(RLIMIT_CORE,&old)==0);puts("core-limit-state: ok");
 int timer=timerfd_create(CLOCK_REALTIME,TFD_NONBLOCK|TFD_CLOEXEC);CHECK(timer>=0);
 struct itimerspec spec={.it_value.tv_sec=INT32_MAX};CHECK(timerfd_settime(timer,TFD_TIMER_ABSTIME|TFD_TIMER_CANCEL_ON_SET,&spec,NULL)==0);
 uint64_t ticks;CHECK(read(timer,&ticks,sizeof(ticks))<0&&errno==EAGAIN);memset(&spec,0,sizeof(spec));CHECK(timerfd_settime(timer,0,&spec,NULL)==0);close(timer);puts("cancel-on-clock-set-timer: ok");unlink(path);return 0;
}
