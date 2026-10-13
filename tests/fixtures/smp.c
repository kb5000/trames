// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern void smp_lse_increment(uint64_t*);
extern void smp_exclusive_increment(uint64_t*);
extern void smp_casp_increment(uint64_t*);
extern void smp_pair_increment(uint64_t*);
static _Alignas(64) uint64_t counter;
static _Alignas(64) uint64_t pair[2] = {0,UINT64_MAX};
static int operation;
static pthread_barrier_t begin;
enum { THREADS=4, ITERATIONS=2000 };
static void *increment(void *arg){
    uintptr_t id=(uintptr_t)arg;static _Thread_local uintptr_t tls;
    tls=id+123;errno=(int)id+50;
    pthread_barrier_wait(&begin);
    for(int i=0;i<ITERATIONS;i++){
        if(operation==0)smp_lse_increment(&counter);
        else if(operation==1)smp_exclusive_increment(&counter);
        else if(operation==2)smp_casp_increment(pair);
        else smp_pair_increment(pair);
    }
    assert(tls==id+123&&errno==(int)id+50);return NULL;
}
static void atomic_tests(void){
    for(operation=0;operation<4;operation++){
        counter=0;pair[0]=0;pair[1]=UINT64_MAX;
        pthread_t threads[THREADS];assert(!pthread_barrier_init(&begin,NULL,THREADS));
        for(uintptr_t i=0;i<THREADS;i++)assert(!pthread_create(&threads[i],NULL,increment,(void*)i));
        for(int i=0;i<THREADS;i++)assert(!pthread_join(threads[i],NULL));
        assert(!pthread_barrier_destroy(&begin));
        if(operation<2)assert(counter==THREADS*ITERATIONS);
        else assert(pair[0]==THREADS*ITERATIONS&&pair[1]==~pair[0]);
    }
    puts("smp: LSE, LL/SC, CASP and pair exclusives; TLS/errno: passed");
}
static _Atomic uint64_t *sequence,*ack;
static uint64_t *payload;
static void *publish(void *unused){
    (void)unused;
    for(uint64_t i=1;i<=500;i++){
        while(atomic_load_explicit(ack,memory_order_acquire)!=i-1){}
        payload[0]=i*13;payload[1]=~(i*13);
        atomic_store_explicit(sequence,i,memory_order_release);
    }return NULL;
}
static void *consume(void *unused){
    (void)unused;
    for(uint64_t i=1;i<=500;i++){
        while(atomic_load_explicit(sequence,memory_order_acquire)!=i){}
        assert(payload[0]==i*13&&payload[1]==~(i*13));
        atomic_store_explicit(ack,i,memory_order_release);
    }return NULL;
}
static void publication(void){
    unsigned char *map=mmap(NULL,12288,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(map!=MAP_FAILED);
    payload=(uint64_t*)map;sequence=(_Atomic uint64_t*)(map+4096);ack=(_Atomic uint64_t*)(map+8192);
    pthread_t a,b;assert(!pthread_create(&a,NULL,publish,NULL));assert(!pthread_create(&b,NULL,consume,NULL));
    assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));assert(!munmap(map,12288));
    puts("smp: acquire/release publication across pages: passed");
}
static _Atomic int x,y;
static int observed[2];
static void *dmb(void *arg){
    int id=(int)(uintptr_t)arg;
    for(int i=0;i<100;i++){
        pthread_barrier_wait(&begin);
        atomic_store_explicit(id?&y:&x,1,memory_order_relaxed);
        __asm__ volatile("dmb ish" ::: "memory");
        observed[id]=atomic_load_explicit(id?&x:&y,memory_order_relaxed);
        pthread_barrier_wait(&begin);
    }return NULL;
}
static void barrier_test(void){
    assert(!pthread_barrier_init(&begin,NULL,3));pthread_t a,b;
    assert(!pthread_create(&a,NULL,dmb,(void*)0));assert(!pthread_create(&b,NULL,dmb,(void*)1));
    for(int i=0;i<100;i++){
        atomic_store(&x,0);atomic_store(&y,0);pthread_barrier_wait(&begin);pthread_barrier_wait(&begin);
        assert(observed[0]||observed[1]);
    }
    assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));assert(!pthread_barrier_destroy(&begin));
    puts("smp: DMB store-buffering forbidden outcome: passed");
}
static _Atomic int ready;
static void *wake(void *unused){
    (void)unused;atomic_store_explicit(&ready,1,memory_order_release);
    assert(syscall(SYS_futex,&ready,1|128,1,NULL,NULL,0)>=0);return NULL;
}
static void futex_test(void){
    for(int i=0;i<100;i++){
        atomic_store(&ready,0);pthread_t t;assert(!pthread_create(&t,NULL,wake,NULL));
        while(!atomic_load_explicit(&ready,memory_order_acquire)){
            int rc=(int)syscall(SYS_futex,&ready,0|128,0,NULL,NULL,0);
            assert(rc==0||(rc==-1&&(errno==EAGAIN||errno==EINTR)));
        }assert(!pthread_join(t,NULL));
    }puts("smp: futex compare/enqueue/wake races: passed");
}
static _Atomic int stop;
static _Atomic uint64_t fork_counter;
static void *fork_writer(void *unused){
    (void)unused;while(!atomic_load_explicit(&stop,memory_order_acquire))atomic_fetch_add_explicit(&fork_counter,1,memory_order_relaxed);return NULL;
}
static void fork_test(void){
    pthread_t t;assert(!pthread_create(&t,NULL,fork_writer,NULL));
    while(atomic_load(&fork_counter)<10){}
    pid_t child=fork();assert(child>=0);
    if(!child){uint64_t before=atomic_load(&fork_counter);for(int i=0;i<1000;i++)assert(atomic_load(&fork_counter)==before);_exit(0);}
    int status;assert(waitpid(child,&status,0)==child);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    atomic_store_explicit(&stop,1,memory_order_release);assert(!pthread_join(t,NULL));
    uint64_t *shared=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(shared!=MAP_FAILED);
    pid_t children[3];for(int i=0;i<3;i++){children[i]=fork();assert(children[i]>=0);if(!children[i]){for(int j=0;j<1000;j++)smp_lse_increment(shared);_exit(0);}}
    for(int i=0;i<3;i++){assert(waitpid(children[i],&status,0)==children[i]);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);}
    assert(*shared==3000);assert(!munmap(shared,4096));
    _Atomic int *phase=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(phase!=MAP_FAILED);
    int pipefd[2];assert(!pipe(pipefd));child=fork();assert(child>=0);
    if(!child){assert(write(pipefd[1],"r",1)==1);while(!atomic_load_explicit(phase,memory_order_acquire)){
        int rc=(int)syscall(SYS_futex,phase,0,0,NULL,NULL,0);assert(rc==0||(rc==-1&&errno==EAGAIN));}_exit(0);}
    char byte;assert(read(pipefd[0],&byte,1)==1);assert(!usleep(10000));atomic_store_explicit(phase,1,memory_order_release);
    assert(syscall(SYS_futex,phase,1,1,NULL,NULL,0)==1);
    assert(waitpid(child,&status,0)==child);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    assert(!close(pipefd[0]));assert(!close(pipefd[1]));assert(!munmap(phase,4096));
    puts("smp: concurrent fork COW and shared process atomics: passed");
}
static void thread_teardown(const char *executable){
    /* exec and exit_group must retire a running peer's returned slice before
       removing its CPU/VM state. A forked child keeps the parent test intact. */
    for(int mode=0;mode<2;mode++){
        pid_t child=fork();assert(child>=0);
        if(!child){
            atomic_store(&stop,0);atomic_store(&fork_counter,0);
            pthread_t t;assert(!pthread_create(&t,NULL,fork_writer,NULL));
            while(atomic_load(&fork_counter)<10){}
            if(mode){
                const char *oracle=getenv("TRAMES_SMP_ORACLE");
                /* QEMU user-mode without host binfmt cannot exec an ARM ELF
                   directly. Re-enter the same pinned oracle in that harness. */
                if(oracle)execl(oracle,oracle,"-cpu","max",executable,"exec-child",(char*)NULL);
                else execl(executable,executable,"exec-child",(char*)NULL);
                _exit(99);
            }
            _exit(42);
        }
        int status;assert(waitpid(child,&status,0)==child);
        assert(WIFEXITED(status)&&WEXITSTATUS(status)==42);
    }
    puts("smp: exec and exit_group with a running pthread peer: passed");
}
static void metadata(void){
    const char *env=getenv("TRAMES_SMP_CPU_COUNT");if(!env)return;
    int n=atoi(env);cpu_set_t cpus;assert(!sched_getaffinity(0,sizeof cpus,&cpus));assert(CPU_COUNT(&cpus)==n);
    assert(sysconf(_SC_NPROCESSORS_ONLN)==n);assert(sysconf(_SC_NPROCESSORS_CONF)==n);
    for(int i=0;i<n;i++){
        CPU_ZERO(&cpus);CPU_SET(i,&cpus);assert(!sched_setaffinity(0,sizeof cpus,&cpus));
        assert(sched_getcpu()==i);
    }
    CPU_ZERO(&cpus);for(int i=0;i<n;i++)CPU_SET(i,&cpus);assert(!sched_setaffinity(0,sizeof cpus,&cpus));
    puts("smp: topology, affinity and getcpu: passed");
}
static int pinned_cpus[2];
static _Atomic int fairness_release;
static _Atomic int fairness_ready;
static void *pinned_waiter(void *arg){
    int id=(int)(uintptr_t)arg;cpu_set_t mask;
    CPU_ZERO(&mask);CPU_SET(pinned_cpus[id],&mask);assert(!sched_setaffinity(0,sizeof mask,&mask));
    pthread_barrier_wait(&begin);
    atomic_fetch_or_explicit(&fairness_ready,1<<id,memory_order_release);
    while(!atomic_load_explicit(&fairness_release,memory_order_acquire)){}
    return NULL;
}
static void affinity_fairness(void){
    cpu_set_t mask;assert(!sched_getaffinity(0,sizeof mask,&mask));
    int found=0;for(int i=0;i<CPU_SETSIZE&&found<2;i++)if(CPU_ISSET(i,&mask))pinned_cpus[found++]=i;
    if(found==1)pinned_cpus[1]=pinned_cpus[0];
    assert(found);atomic_store(&fairness_release,0);atomic_store(&fairness_ready,0);
    assert(!pthread_barrier_init(&begin,NULL,3));pthread_t threads[2];
    for(uintptr_t i=0;i<2;i++)assert(!pthread_create(&threads[i],NULL,pinned_waiter,(void*)i));
    pthread_barrier_wait(&begin);
    /* Both pinned peers spin until this flexible task can run. */
    while(atomic_load_explicit(&fairness_ready,memory_order_acquire)!=3){}
    atomic_store_explicit(&fairness_release,1,memory_order_release);
    for(int i=0;i<2;i++)assert(!pthread_join(threads[i],NULL));
    assert(!pthread_barrier_destroy(&begin));
    puts("smp: pinned peers cannot starve a flexible waiter: passed");
}
static int loops;
static uint64_t results[16];
static void *compute(void *arg){
    uintptr_t id=(uintptr_t)arg;uint64_t v=id+1;
    for(int i=0;i<loops;i++){v^=v<<13;v^=v>>7;v^=v<<17;v+=0x9e3779b97f4a7c15ULL;}
    results[id]=v;return NULL;
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"exec-child"))return 42;
    if(argc==2&&!strcmp(argv[1],"fairness")){metadata();affinity_fairness();return 0;}
    if(argc==4&&!strcmp(argv[1],"work")){
        int count=atoi(argv[2]);loops=atoi(argv[3]);assert(count>0&&count<=16&&loops>0);
        pthread_t threads[16];for(uintptr_t i=0;i<(uintptr_t)count;i++)assert(!pthread_create(&threads[i],NULL,compute,(void*)i));
        for(int i=0;i<count;i++)assert(!pthread_join(threads[i],NULL));
        uint64_t result=0;for(int i=0;i<count;i++)result^=results[i];printf("smp work: %016llx\n",(unsigned long long)result);return 0;
    }
    assert(argc==1);metadata();affinity_fairness();atomic_tests();publication();barrier_test();futex_test();fork_test();
    const char *executable=getenv("TRAMES_SMP_EXEC_PATH");
    thread_teardown(executable?executable:argv[0]);return 0;
}
