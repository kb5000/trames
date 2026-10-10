// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <unistd.h>
static volatile sig_atomic_t signals;
static void handler(int sig) {(void)sig;signals++;}
#define CHECK(expr) do {if(!(expr)){fprintf(stderr,"check line %d errno %d\n",__LINE__,errno);return 1;}}while(0)
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc>1 && !strcmp(argv[1],"child")) {CHECK(argc==5);int kept=atoi(argv[3]),closed=atoi(argv[4]);CHECK(fcntl(kept,F_GETFD)>=0);errno=0;CHECK(fcntl(closed,F_GETFD)==-1&&errno==EBADF);printf("exec %s %s cloexec\n",argv[2],getenv("PROBE"));return 17;}
    CHECK(argc==2);
    volatile int private=7;
    pid_t child=fork();CHECK(child>=0);
    if(child==0){private=42;_exit(private==42?5:1);}
    int status;CHECK(waitpid(child,&status,0)==child && WIFEXITED(status)&&WEXITSTATUS(status)==5 && private==7);puts("fork cow wait");
    int file=open("shared",O_CREAT|O_RDWR|O_TRUNC,0600);CHECK(file>=0 && write(file,"abcd",4)==4 && lseek(file,0,SEEK_SET)==0);
    child=fork();CHECK(child>=0);if(child==0){char c;_exit(read(file,&c,1)==1&&c=='a'?0:1);}CHECK(waitpid(child,&status,0)==child&&status==0);char c;CHECK(read(file,&c,1)==1&&c=='b');close(file);puts("shared offset");
    int pipefd[2];CHECK(pipe2(pipefd,O_CLOEXEC)==0);child=fork();CHECK(child>=0);
    if(child==0){close(pipefd[0]);unsigned char data[4096];memset(data,0xa5,sizeof data);for(int i=0;i<64;i++){size_t sent=0;while(sent<sizeof data){ssize_t n=write(pipefd[1],data+sent,sizeof data-sent);if(n<=0)_exit(1);sent+=(size_t)n;}}close(pipefd[1]);_exit(0);}
    close(pipefd[1]);unsigned long total=0,sum=0;unsigned char buffer[777];ssize_t n;while((n=read(pipefd[0],buffer,sizeof buffer))>0){total+=(unsigned long)n;for(ssize_t i=0;i<n;i++)sum+=buffer[i];}CHECK(n==0 && total==262144 && sum==43253760);close(pipefd[0]);CHECK(waitpid(child,&status,0)==child&&status==0);puts("pipe backpressure eof");
    struct sigaction action={0};action.sa_handler=handler;action.sa_flags=SA_RESTART;sigemptyset(&action.sa_mask);CHECK(sigaction(SIGUSR1,&action,NULL)==0);
    sigset_t set;sigemptyset(&set);sigaddset(&set,SIGUSR1);CHECK(sigprocmask(SIG_BLOCK,&set,NULL)==0 && kill(getpid(),SIGUSR1)==0 && signals==0);sigset_t pending;CHECK(sigpending(&pending)==0&&sigismember(&pending,SIGUSR1));CHECK(sigprocmask(SIG_UNBLOCK,&set,NULL)==0 && signals==1);puts("signal mask frame return");
    CHECK(pipe(pipefd)==0);child=fork();CHECK(child>=0);if(child==0){close(pipefd[0]);usleep(10000);kill(getppid(),SIGUSR1);usleep(10000);if(write(pipefd[1],"R",1)!=1)_exit(1);_exit(0);}
    close(pipefd[1]);CHECK(read(pipefd[0],&c,1)==1&&c=='R'&&signals==2);close(pipefd[0]);CHECK(waitpid(child,&status,0)==child&&status==0);puts("signal restart read");
    CHECK(pipe(pipefd)==0);child=fork();CHECK(child>=0);if(child==0){close(pipefd[1]);raise(SIGSTOP);if(read(pipefd[0],&c,1)!=1)_exit(1);_exit(3);}
    close(pipefd[0]);CHECK(waitpid(child,&status,WUNTRACED)==child&&WIFSTOPPED(status));CHECK(kill(child,SIGCONT)==0);CHECK(waitpid(child,&status,WCONTINUED)==child&&WIFCONTINUED(status));CHECK(write(pipefd[1],"x",1)==1);close(pipefd[1]);CHECK(waitpid(child,&status,0)==child&&WEXITSTATUS(status)==3);puts("stop continue wait");
    CHECK(pipe2(pipefd,O_NONBLOCK)==0);struct pollfd pollfd={pipefd[0],POLLIN,0};CHECK(poll(&pollfd,1,0)==0);CHECK(write(pipefd[1],"x",1)==1);CHECK(poll(&pollfd,1,0)==1&&(pollfd.revents&POLLIN));CHECK(read(pipefd[0],&c,1)==1);close(pipefd[1]);CHECK(poll(&pollfd,1,0)==1&&(pollfd.revents&POLLHUP));close(pipefd[0]);puts("poll pipe readiness");
    int kept=open("/dev/null",O_RDONLY),closed=open("/dev/null",O_RDONLY|O_CLOEXEC);CHECK(kept>=0&&closed>=0);char kept_text[16],closed_text[16];snprintf(kept_text,sizeof kept_text,"%d",kept);snprintf(closed_text,sizeof closed_text,"%d",closed);
    child=fork();CHECK(child>=0);if(child==0){char *arguments[]={argv[1],"child","value",kept_text,closed_text,NULL};char *environment[]={"PROBE=clean",NULL};execve(argv[1],arguments,environment);_exit(111);}
    CHECK(waitpid(child,&status,0)==child&&WEXITSTATUS(status)==17);puts("execve image argv env");close(kept);close(closed);
    CHECK(pipe(pipefd)==0);fd_set readable;FD_ZERO(&readable);FD_SET(pipefd[0],&readable);struct timeval timeout={0};CHECK(select(pipefd[0]+1,&readable,NULL,NULL,&timeout)==0);CHECK(write(pipefd[1],"s",1)==1);FD_ZERO(&readable);FD_SET(pipefd[0],&readable);timeout.tv_sec=timeout.tv_usec=0;CHECK(select(pipefd[0]+1,&readable,NULL,NULL,&timeout)==1&&FD_ISSET(pipefd[0],&readable));close(pipefd[0]);close(pipefd[1]);puts("select readiness");
    action.sa_handler=SIG_IGN;action.sa_flags=0;CHECK(sigaction(SIGPIPE,&action,NULL)==0);CHECK(pipe(pipefd)==0);close(pipefd[0]);errno=0;CHECK(write(pipefd[1],"x",1)==-1&&errno==EPIPE);close(pipefd[1]);puts("broken pipe errno");
    CHECK(sigaction(SIGCHLD,&action,NULL)==0);child=fork();CHECK(child>=0);if(child==0)_exit(0);errno=0;CHECK(waitpid(child,&status,0)==-1&&errno==ECHILD);puts("ignored sigchld reap");
    return 0;
}
