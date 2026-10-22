// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#ifdef TRAMES_HOST_ORACLE
static void *native_fill(void *p, int c, size_t n) { return memset(p,c,n); }
#else
extern void *native_fill(void *,int,size_t);
#endif
static unsigned rounds;
static pthread_barrier_t barrier;
static uint32_t checksum;
static void *compute(void *unused) {
    (void)unused;
    size_t size=1<<20;
    unsigned char *bytes=malloc(size); assert(bytes);
    int rc=pthread_barrier_wait(&barrier);
    assert(!rc || rc==PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned i=0;i<rounds*16;i++) {
        unsigned char v=(unsigned char)(i*31+7);
        assert(native_fill(bytes,v,size)==bytes);
        assert(bytes[0]==v && bytes[size/2]==v && bytes[size-1]==v);
        checksum=checksum*33+v;
    }
    free(bytes); return NULL;
}
int main(int argc,char **argv) {
    assert(argc==4);
    int mode=!strcmp(argv[1],"timer")?0:!strcmp(argv[1],"epoll")?1:!strcmp(argv[1],"ppoll")?2:3;
    assert(mode!=3 || !strcmp(argv[1],"timeout"));
    rounds=(unsigned)strtoul(argv[3],NULL,10); assert(rounds && rounds<=1024);
    if (!strcmp(argv[2],"pinned")) {
        cpu_set_t cpus; CPU_ZERO(&cpus);
#ifdef TRAMES_HOST_ORACLE
        assert(!sched_getaffinity(0,sizeof cpus,&cpus));
        int cpu; for (cpu=0;cpu<CPU_SETSIZE && !CPU_ISSET(cpu,&cpus);cpu++) {}
        assert(cpu<CPU_SETSIZE); CPU_ZERO(&cpus); CPU_SET(cpu,&cpus);
#else
        CPU_SET(0,&cpus);
#endif
        assert(!sched_setaffinity(0,sizeof cpus,&cpus));
    } else assert(!strcmp(argv[2],"spread"));
    int timer=timerfd_create(CLOCK_MONOTONIC,TFD_CLOEXEC);
    int event=eventfd(0,EFD_CLOEXEC);
    assert(timer>=0 && event>=0);
    int ep=-1;
    if (mode==1) {
        ep=epoll_create1(EPOLL_CLOEXEC); assert(ep>=0);
        struct epoll_event watch={.events=EPOLLIN,.data.u64=42};
        assert(!epoll_ctl(ep,EPOLL_CTL_ADD,timer,&watch));
        watch.data.u64=43; assert(!epoll_ctl(ep,EPOLL_CTL_ADD,event,&watch));
    }
    assert(!pthread_barrier_init(&barrier,NULL,2));
    pthread_t thread; assert(!pthread_create(&thread,NULL,compute,NULL));
    int rc=pthread_barrier_wait(&barrier);
    assert(!rc || rc==PTHREAD_BARRIER_SERIAL_THREAD);
    uint64_t total=0;
    for (unsigned i=0;i<rounds;i++) {
        struct timespec timeout={.tv_nsec=10000};
        if (mode==3) {
            assert(ppoll(NULL,0,&timeout,NULL)==0);
            total++;
            continue;
        }
        struct itimerspec spec={.it_value={.tv_nsec=10000}},old={0};
        assert(!timerfd_settime(timer,0,&spec,&old));
        assert(!old.it_interval.tv_sec && !old.it_interval.tv_nsec);
        assert(!timerfd_gettime(timer,&old));
        assert(!old.it_interval.tv_sec && !old.it_interval.tv_nsec);
        if (mode==1) {
            struct epoll_event ready;
            assert(epoll_wait(ep,&ready,1,-1)==1 && ready.data.u64==42 && ready.events&EPOLLIN);
        } else if (mode==2) {
            struct pollfd fds[]={{timer,POLLIN,0},{event,POLLIN,0},{-1,POLLIN,0}};
            struct timespec limit={.tv_sec=1};
            assert(ppoll(fds,3,&limit,NULL)==1);
            assert(fds[0].revents==POLLIN && !fds[1].revents && !fds[2].revents);
        }
        uint64_t ticks=0;
        assert(read(timer,&ticks,8)==8 && ticks==1);
        total+=ticks;
    }
    assert(!pthread_join(thread,NULL));
    assert(total==rounds);
    assert(!close(timer) && !close(event));
    if (ep>=0) assert(!close(ep));
    printf("worker-wait %s %s %u %llu %08x\n",argv[1],argv[2],rounds,(unsigned long long)total,checksum);
    return 0;
}
