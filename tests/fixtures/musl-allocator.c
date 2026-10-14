// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/auxv.h>
#include <sys/wait.h>
#include <unistd.h>

static pthread_barrier_t barrier;
static unsigned char *handoff[4][192];
static unsigned rounds=12;
static int bench;
static size_t length(unsigned i) { return 1+(i*73)%2048; }
static unsigned char mark(unsigned owner,unsigned i) { return 1+(owner*19+i)%254; }
static void *worker(void *arg) {
    unsigned id=(uintptr_t)arg;
    for (unsigned r=0;r<rounds;r++) {
        for(unsigned i=0;i<192;i++) {
            size_t n=length(i);errno=EDOM;
            unsigned char *p=malloc(n);assert(p && !((uintptr_t)p&15));
            assert(errno==EDOM && malloc_usable_size(p)>=n);
            memset(p,mark(id,i),n);handoff[id][i]=p;
        }
        int b=pthread_barrier_wait(&barrier);assert(!b || b==PTHREAD_BARRIER_SERIAL_THREAD);
        unsigned other=(id+1)%4;
        for(unsigned i=0;i<192;i++) {
            unsigned char *p=handoff[other][i];size_t n=length(i);
            if(bench)assert(p[0]==mark(other,i) && p[n-1]==mark(other,i));
            else for(size_t j=0;j<n;j++)assert(p[j]==mark(other,i));
            errno=E2BIG;free(p);assert(errno==E2BIG);
        }
        b=pthread_barrier_wait(&barrier);assert(!b || b==PTHREAD_BARRIER_SERIAL_THREAD);
    }
    return 0;
}
static void boundaries(void) {
    for(size_t n=0;n<4096;n+=13) {
        unsigned char *p=calloc(n,1);assert(p);for(size_t i=0;i<n;i++)assert(!p[i]);
        memset(p,0x73,n);unsigned char *q=realloc(p,n+5000);assert(q);
        for(size_t i=0;i<n;i++)assert(q[i]==0x73);
        q=realloc(q,n+1);assert(q);for(size_t i=0;i<n;i++)assert(q[i]==0x73);free(q);
    }
    for(size_t alignment=16;alignment<=1048576;alignment*=2) {
        void *p=0;assert(!posix_memalign(&p,alignment,177));assert(!((uintptr_t)p&(alignment-1)));memset(p,0xa4,177);free(p);
        p=aligned_alloc(alignment,alignment);assert(p && !((uintptr_t)p&(alignment-1)));free(p);
    }
    volatile size_t impossible=SIZE_MAX;errno=0;assert(!malloc(impossible) && errno==ENOMEM);
    errno=0;assert(!calloc(impossible,2) && errno==ENOMEM);
    unsigned char *p=malloc(200000);assert(p);memset(p,0x51,200000);
    p=realloc(p,400000);assert(p);for(size_t i=0;i<200000;i++)assert(p[i]==0x51);free(p);
    errno=EINTR;free(0);assert(errno==EINTR);
}
int main(int argc,char **argv) {
    if(argc>1 && !strcmp(argv[1],"exec-probe")) {
        assert(argc==3 && getauxval(0x54520007)==strtoul(argv[2],0,10));
        boundaries();_exit(0);
    }
    if(argc>1 && !strcmp(argv[1],"bench")){bench=1;rounds=argc>2?strtoul(argv[2],0,10):48;}
    else boundaries();
    assert(rounds>0 && rounds<=96);pthread_t threads[4];void *stacks[4]={0};
    assert(!pthread_barrier_init(&barrier,0,4));
    for(unsigned i=0;i<4;i++) {
        pthread_attr_t attr;assert(!pthread_attr_init(&attr));
        if(argc>1 && !strcmp(argv[1],"shared-stack")) {
            stacks[i]=mmap(0,262144,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(stacks[i]!=MAP_FAILED);
            assert(!pthread_attr_setstack(&attr,stacks[i],262144));
        }
        assert(!pthread_create(&threads[i],&attr,worker,(void *)(uintptr_t)i));assert(!pthread_attr_destroy(&attr));
    }
    for(unsigned i=0;i<4;i++){assert(!pthread_join(threads[i],0));if(stacks[i])assert(!munmap(stacks[i],262144));}
    assert(!pthread_barrier_destroy(&barrier));
    unsigned char *p=malloc(157);assert(p);memset(p,0x81,157);pid_t child=fork();assert(child>=0);
    if(!child){assert(p[0]==0x81);p[0]=0x91;free(p);void *q=malloc(157);assert(q);free(q);_exit(0);}
    int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
    assert(p[0]==0x81);free(p);
    if(!getenv("TRAMES_ORACLE_SKIP_EXEC")) {
        char profile[32];snprintf(profile,sizeof profile,"%lu",getauxval(0x54520007));
        child=fork();assert(child>=0);
        if(!child){execl(argv[0],argv[0],"exec-probe",profile,(char *)0);_exit(7);}
        assert(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
    }
    puts("musl-allocator-ok");return 0;
}
