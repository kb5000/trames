// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int64_t gpu_call(uint64_t, const uint64_t *, uint64_t);
extern void *native_fill(void *, int, size_t);
static pthread_barrier_t barrier;
static uint32_t checksum;
static int64_t request(uint64_t op, const uint64_t *args, size_t count) {
    uint64_t packet[16]={0};
    assert(count<=16); memcpy(packet,args,count*8);
    return gpu_call(op,packet,sizeof packet);
}
static void complete(uint64_t op, const uint64_t *args, size_t count) {
    int64_t r=request(op,args,count);
    if (r) { fprintf(stderr,"worker-gpu op=%llu result=%lld\n",(unsigned long long)op,(long long)r); exit(1); }
}
#define G(op,...) do { const uint64_t args[]={__VA_ARGS__}; complete(op,args,sizeof args/8); } while(0)
#define P(x) ((uint64_t)(uintptr_t)(x))
static void *compute(void *unused) {
    (void)unused;
    unsigned char *bytes=malloc(1<<20); assert(bytes);
    int rc=pthread_barrier_wait(&barrier); assert(!rc || rc==PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned i=0;i<1024;i++) {
        unsigned char v=(unsigned char)(i*31+7);
        assert(native_fill(bytes,v,1<<20)==bytes);
        assert(bytes[0]==v && bytes[(1<<20)-1]==v);
        checksum=checksum*33+v;
    }
    free(bytes); return NULL;
}
int main(int argc,char **argv) {
    assert(argc==3 && (!strcmp(argv[1],"query") || !strcmp(argv[1],"fill")));
    int fill=!strcmp(argv[1],"fill");
    if (!strcmp(argv[2],"pinned")) {
        cpu_set_t cpus; CPU_ZERO(&cpus); CPU_SET(0,&cpus);
        assert(!sched_setaffinity(0,sizeof cpus,&cpus));
    } else assert(!strcmp(argv[2],"spread"));
    uint64_t instance=0,physical=0,device=0,queue=0,buffer=0,allocation=0,pool=0,cmd=0,fence=0;
    uint32_t *mapped=NULL;
    if (fill) {
        G(1,P(&instance));
        uint32_t count=1; G(3,instance,P(&count),P(&physical)); assert(count==1);
        G(8,physical,0,P(&device)); G(10,device,0,0,P(&queue));
        G(11,device,4096,2,P(&buffer));
        uint64_t req[3]={0}; G(13,device,buffer,P(req));
        uint64_t types[65]={0}; G(5,physical,P(types));
        const uint32_t *words=(const uint32_t *)types;
        uint32_t ty=0;
        for (;ty<words[0];ty++) if ((req[2]&(1ull<<ty)) && (words[1+2*ty]&6)==6) break;
        assert(ty<words[0]);
        G(14,device,req[0],ty,P(&allocation)); G(16,device,buffer,allocation,0);
        assert(!posix_memalign((void **)&mapped,4096,4096)); memset(mapped,0,4096);
        G(17,device,allocation,0,4096,P(mapped));
        G(34,device,0,2,P(&pool)); G(36,device,pool,P(&cmd)); G(48,device,0,P(&fence));
    }
    assert(!pthread_barrier_init(&barrier,NULL,2));
    pthread_t thread; assert(!pthread_create(&thread,NULL,compute,NULL));
    int rc=pthread_barrier_wait(&barrier); assert(!rc || rc==PTHREAD_BARRIER_SERIAL_THREAD);
    uint32_t gpu_checksum=0;
    for (unsigned i=0;i<32;i++) {
        if (!fill) {
            uint64_t info[4]={1,2,3,0xa5}; G(79,P(info));
            assert(!info[0] && !info[1] && !info[2] && info[3]==0xa5);
            continue;
        }
        uint32_t expected=0x12345600+i;
        G(40,device,cmd,0); G(38,device,cmd,0);
        G(46,device,cmd,buffer,0,4096,expected); G(39,device,cmd);
        G(50,device,fence); G(52,device,queue,P(&cmd),1,fence);
        for (unsigned n=0;;n++) {
            uint64_t args[]={device,fence}; int64_t r=request(51,args,2);
            if (!r) break;
            assert(r==1 && n<100000); sched_yield();
        }
        for (unsigned n=0;n<1024;n++) assert(mapped[n]==expected);
        gpu_checksum=gpu_checksum*33+mapped[0];
    }
    assert(!pthread_join(thread,NULL));
    if (fill) {
        G(18,device,allocation); G(49,device,fence); G(35,device,pool);
        G(12,device,buffer); G(15,device,allocation); G(9,device); G(2,instance);
        free(mapped);
    }
    printf("worker-gpu %s %s 32 %08x %08x\n",argv[1],argv[2],checksum,gpu_checksum);
    return 0;
}
