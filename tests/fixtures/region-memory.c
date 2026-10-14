// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

struct job { uint64_t *counter; unsigned iterations; };
static pthread_barrier_t start;
static void *work(void *argument) {
    struct job *job=argument;
    int result=pthread_barrier_wait(&start);
    assert(result==0 || result==PTHREAD_BARRIER_SERIAL_THREAD);
    for(unsigned i=0;i<job->iterations;i++) {
        uint64_t value;
        // Ordinary aligned guest load/store. Each thread owns a disjoint
        // 64-byte region of one VM page, without guest atomic RMWs or libc calls.
        __asm__ volatile("ldr %0, [%1]\nadd %0, %0, #1\nstr %0, [%1]"
            : "=&r"(value) : "r"(job->counter) : "memory");
    }
    return 0;
}
int main(int argc,char **argv) {
    unsigned iterations=argc>1?(unsigned)strtoul(argv[1],0,10):400000;
    assert(iterations && iterations<=4000000);
    unsigned char *page=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(page!=MAP_FAILED);
    struct job jobs[4];pthread_t threads[4];assert(!pthread_barrier_init(&start,0,5));
    for(unsigned i=0;i<4;i++) {
        jobs[i]=(struct job){.counter=(uint64_t *)(page+i*64),.iterations=iterations};
        assert(!pthread_create(&threads[i],0,work,&jobs[i]));
    }
    int result=pthread_barrier_wait(&start);
    assert(result==0 || result==PTHREAD_BARRIER_SERIAL_THREAD);
    uint64_t sum=0;
    for(unsigned i=0;i<4;i++) {
        assert(!pthread_join(threads[i],0));assert(*jobs[i].counter==iterations);sum+=*jobs[i].counter;
    }
    assert(!pthread_barrier_destroy(&start));assert(!munmap(page,4096));
    printf("region-memory-ok %llu\n",(unsigned long long)sum);return 0;
}
