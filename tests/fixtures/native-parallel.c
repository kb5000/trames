// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

struct job { unsigned id, iterations; size_t bytes; int bench; unsigned char *a,*b; uint64_t result; };
static pthread_barrier_t start;
static _Thread_local int nested_done;
static int integers(const void *a,const void *b) {
    uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;return (x>y)-(x<y);
}
static int numbers(const void *a,const void *b) {
    if(!nested_done) {
        nested_done=1;uint64_t inner[64];for(unsigned i=0;i<64;i++)inner[i]=63-i;
        qsort(inner,64,sizeof(*inner),integers);for(unsigned i=0;i<64;i++)assert(inner[i]==i);
    }
    char *end;int saved=errno;
    long x=strtol(a,&end,10);assert(*end=='!' && errno==saved);
    long y=strtol(b,&end,10);assert(*end=='!' && errno==saved);return (x>y)-(x<y);
}
static void *run(void *arg) {
    struct job *j=arg;int r=pthread_barrier_wait(&start);assert(r==0 || r==PTHREAD_BARRIER_SERIAL_THREAD);
    if(j->bench) {
        for(unsigned i=0;i<j->iterations;i++) {
            assert(memcpy(j->b,j->a,j->bytes)==j->b);
            if((i&7)==7)assert(!memcmp(j->a,j->b,j->bytes));
        }
        assert(!memcmp(j->a,j->b,j->bytes));
        j->result=(uint64_t)j->bytes*j->iterations+j->b[0]+j->b[j->bytes-1];return 0;
    }
    char text[512],out[2048],other[2048],number[160],real[160];
    memset(text,'a',511);text[511]=0;memset(number,' ',128);snprintf(number+128,32,"%u!",1234+j->id);
    memset(real,' ',128);memcpy(real+128,"12.5!",6);
    for(unsigned i=0;i<j->iterations;i++) {
        unsigned char byte=0x40+j->id+i;assert(memset(j->a,byte,j->bytes)==j->a);
        assert(memcpy(j->b,j->a,j->bytes)==j->b);assert(memmove(j->b+1,j->b,j->bytes-1)==j->b+1);
        assert(!memcmp(j->a,j->b,j->bytes));assert(memchr(j->b,byte,j->bytes)==j->b);assert(memrchr(j->b,byte,j->bytes)==j->b+j->bytes-1);
        assert(strcpy(out,text)==out);assert(stpcpy(other,text)==other+511);assert(strlen(out)==511 && strnlen(out,SIZE_MAX)==511);
        assert(!strcmp(out,other) && !strncmp(out,other,SIZE_MAX));assert(!strchr(out,'z'));assert(strrchr(out,'a')==out+510 && strchrnul(out,'z')==out+511);
        assert(strncpy(out,text,1024)==out && !memcmp(out,text,512) && out[1023]==0);
        assert(stpncpy(other,text,1024)==other+511 && other[1023]==0);
        assert(strcpy(out,text)==out);assert(strcat(out,text)==out);assert(strncat(out,text,128)==out);assert(strlen(out)==1150 && out[1150]==0);
        char *end;errno=EDOM;assert(strtol(number,&end,10)==1234+j->id && *end=='!' && errno==EDOM);
        uint64_t fpcr,fpsr;__asm__ volatile("mrs %0, fpcr\n mrs %1, fpsr":"=r"(fpcr),"=r"(fpsr));
        uint64_t flags=j->id&3;__asm__ volatile("msr fpsr, %0"::"r"(flags):"memory");
        errno=ERANGE;assert(strtod(real,&end)==12.5 && *end=='!' && errno==ERANGE);assert(strtof(real,&end)==12.5f && *end=='!' && errno==ERANGE);
        uint64_t after_fpcr,after_fpsr;__asm__ volatile("mrs %0, fpcr\n mrs %1, fpsr":"=r"(after_fpcr),"=r"(after_fpsr));assert(after_fpcr==fpcr && after_fpsr==flags);
        __asm__ volatile("msr fpsr, %0"::"r"(fpsr):"memory");
        j->result+=j->b[0]+j->b[j->bytes-1]+1150+1234+j->id;
    }
    char rows[64][128];for(unsigned i=0;i<64;i++)snprintf(rows[i],128,"%u!",63-i);
    qsort(rows,64,128,numbers);for(unsigned i=0;i<64;i++)assert(strtol(rows[i],0,10)==i);
    assert(bsearch("17!",rows,64,128,numbers)==rows[17]);return 0;
}
int main(int argc,char **argv) {
    int bench=argc>1 && !strcmp(argv[1],"bench"),shared=argc>1 && !strcmp(argv[1],"shared-stack");
    unsigned iterations=bench?(argc>2?(unsigned)strtoul(argv[2],0,10):128):4;
    size_t bytes=bench?1048576:65536;assert(iterations>0 && iterations<=512);
    struct job jobs[4];pthread_t threads[4];void *stacks[4]={0};assert(!pthread_barrier_init(&start,0,5));
    for(unsigned i=0;i<4;i++) {
        jobs[i]=(struct job){.id=i,.iterations=iterations,.bytes=bytes,.bench=bench,.a=malloc(bytes),.b=malloc(bytes)};assert(jobs[i].a && jobs[i].b);
        memset(jobs[i].a,0x51+i,bytes);memset(jobs[i].b,0,bytes);
        pthread_attr_t attr;assert(!pthread_attr_init(&attr));
        if(shared){stacks[i]=mmap(0,262144,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(stacks[i]!=MAP_FAILED);assert(!pthread_attr_setstack(&attr,stacks[i],262144));}
        assert(!pthread_create(&threads[i],&attr,run,&jobs[i]));assert(!pthread_attr_destroy(&attr));
    }
    int r=pthread_barrier_wait(&start);assert(r==0 || r==PTHREAD_BARRIER_SERIAL_THREAD);uint64_t result=0;
    for(unsigned i=0;i<4;i++){assert(!pthread_join(threads[i],0));result+=jobs[i].result;free(jobs[i].a);free(jobs[i].b);if(shared)assert(!munmap(stacks[i],262144));}
    assert(!pthread_barrier_destroy(&start));printf("native-parallel-ok %llu\n",(unsigned long long)result);return 0;
}
