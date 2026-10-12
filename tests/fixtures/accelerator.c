// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
static void *(*cp)(void*,const void*,size_t) = memcpy;
static void *(*mv)(void*,const void*,size_t) = memmove;
static void *(*st)(void*,int,size_t) = memset;
static int (*cmp)(const void*,const void*,size_t) = memcmp;
static size_t (*len)(const char*) = strlen;
static sigjmp_buf jump;
static volatile sig_atomic_t fault_signal;
static volatile uintptr_t fault_address;
static void fault(int sig, siginfo_t *info, void *context) {
    (void)context; fault_signal=sig; fault_address=(uintptr_t)info->si_addr; siglongjmp(jump,1);
}
static void *thread(void *unused) {
    (void)unused; char a[8192],b[8192]; st(a,0x91,sizeof(a)); cp(b,a,sizeof(a)); assert(!cmp(a,b,sizeof(a))); return 0;
}
int main(int argc, char **argv) {
#ifdef TRAMES_DYNAMIC
#ifdef TRAMES_FULL_LIBRARY
    void *library=dlopen("libc.so",RTLD_NOW|RTLD_LOCAL);
#else
    void *library=dlopen("/lib/libtrames-pure.so",RTLD_NOW|RTLD_LOCAL);
#endif
    assert(library);
    cp=dlsym(library,"memcpy"); mv=dlsym(library,"memmove"); st=dlsym(library,"memset"); cmp=dlsym(library,"memcmp"); len=dlsym(library,"strlen");
    assert(cp && mv && st && cmp && len);
#endif
    if(argc>1 && !strcmp(argv[1],"exec-child")) { char a[32],b[32]; st(a,0x62,32); cp(b,a,32); assert(!cmp(a,b,32)); return 0; }
    if (argc>1) {
        size_t n=(size_t)strtoul(argv[1],0,10); assert(n && n<=65536);
        unsigned char *a=malloc(n),*b=malloc(n); assert(a && b); st(a,0x51,n);
        for (int i=0;i<256;i++) { assert(cp(b,a,n)==b); if(argc<3 || strcmp(argv[2],"copy")) assert(!cmp(a,b,n)); }
        assert(!cmp(a,b,n));
        puts("accelerator-bench-ok"); return 0;
    }
    size_t sizes[]={0,1,3,7,8,15,16,31,32,63,64,95,127,128,129,511,4095,4096,65535,65536,65537,80000};
    unsigned char *a=malloc(100032),*b=malloc(100032),*expected=malloc(100032);
    assert(a && b && expected);
    for (unsigned align=0;align<8;align++) for (unsigned k=0;k<sizeof(sizes)/sizeof(*sizes);k++) {
        size_t n=sizes[k]; st(a,0xc7,100032); st(b,0x1a,100032);
        for(size_t i=0;i<n;i++) a[align+i]=(unsigned char)(i*73u+1);
        assert(cp(b+7-align,a+align,n)==b+7-align); assert(!cmp(b+7-align,a+align,n));
        if(7-align) assert(b[6-align]==0x1a);
        assert(b[7-align+n]==0x1a);
        assert(st(b+align,0x180,n)==b+align);
        for(size_t i=0;i<n;i++) assert(b[align+i]==0x80);
    }
    for(size_t shift=1;shift<=32;shift++) {
        for(size_t i=0;i<8192;i++) a[i]=expected[i]=(unsigned char)i;
        for(size_t i=8192-shift;i>0;i--) expected[i-1+shift]=expected[i-1];
        mv(a+shift,a,8192-shift); assert(!cmp(a,expected,8192));
        for(size_t i=0;i<8192-shift;i++) expected[i]=expected[i+shift];
        mv(a,a+shift,8192-shift); assert(!cmp(a,expected,8192));
    }
    assert(cp((void*)1,(void*)1,0)==(void*)1); assert(st((void*)1,255,0)==(void*)1);
    assert(mv((void*)1,(void*)1,32)==(void*)1);
    assert(cmp((void*)1,(void*)2,0)==0);
    unsigned char hi[]={0xff},lo[]={1}; assert(cmp(hi,lo,1)>0 && cmp(lo,hi,1)<0);
    size_t lengths[]={0,1,7,8,15,31,4095,4096,65535,65536,80000};
    for(unsigned i=0;i<sizeof(lengths)/sizeof(*lengths);i++) { st(a,'q',lengths[i]); a[lengths[i]]=0; assert(len((char*)a)==lengths[i]); }
    errno=EDOM; cp(b,a,32); assert(errno==EDOM);
    struct sigaction action={.sa_sigaction=fault,.sa_flags=SA_SIGINFO}; sigemptyset(&action.sa_mask);
    assert(!sigaction(SIGSEGV,&action,0)); assert(!sigaction(SIGBUS,&action,0)); assert(!sigaction(SIGILL,&action,0));
    unsigned char *pages=mmap(0,12288,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(pages!=MAP_FAILED);
    assert(!mprotect(pages+8192,4096,PROT_NONE)); pages[8191]=0;
    assert(len((char*)pages+8191)==0); // NUL in final readable byte.
    pages[8191]=1; pages[4095]=2; assert(cmp(pages+8191,pages+4095,32)<0); // mismatch before inaccessible page.
    if(!sigsetjmp(jump,1)) { len((char*)pages+8191); assert(0); }
    assert(fault_signal==SIGSEGV && fault_address==(uintptr_t)pages+8192);
    if(!sigsetjmp(jump,1)) { cp(b,pages+8191,2); assert(0); }
    assert(fault_signal==SIGSEGV && fault_address==(uintptr_t)pages+8192);
#ifdef TRAMES_FULL_LIBRARY
    if(!sigsetjmp(jump,1)) { cp(b,pages+8191,128); assert(0); }
    assert(fault_signal==SIGSEGV && fault_address==(uintptr_t)pages+8192);
#endif
    // Native writes preserve fork COW and shared mappings.
    char *shared=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0); assert(shared!=MAP_FAILED);
    pages[0]=7; pid_t child=fork(); assert(child>=0);
    if(!child) { st(pages,9,4096); st(shared,0x52,4096); _exit(0); }
    int status; assert(waitpid(child,&status,0)==child && status==0); assert(pages[0]==7 && shared[0]==0x52);
    if(!getenv("TRAMES_ORACLE_SKIP_EXEC")) {
    child=fork(); assert(child>=0);
    if(!child) { execl("/bin/accelerator-test","accelerator-test","exec-child",(char*)0); _exit(99); }
    assert(waitpid(child,&status,0)==child && status==0);
    }
    pthread_t tid; assert(!pthread_create(&tid,0,thread,0)); assert(!pthread_join(tid,0));
    // Executable-page writes invalidate already-decoded instructions.
    uint32_t *code=mmap(0,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(code!=MAP_FAILED);
    uint32_t first[]={0x52800020,0xd65f03c0},second[]={0x52800040,0xd65f03c0};
    cp(code,first,sizeof(first)); __builtin___clear_cache((char*)code,(char*)code+8); assert(((int(*)(void))code)()==1);
    cp(code,second,sizeof(second)); __builtin___clear_cache((char*)code,(char*)code+8); assert(((int(*)(void))code)()==2);
    // A coherent file mapping remains observable through a second alias and fd.
    int fd=(int)syscall(SYS_memfd_create,"native",0); assert(fd>=0 && !ftruncate(fd,4096));
    char *m1=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0),*m2=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); assert(m1!=MAP_FAILED && m2!=MAP_FAILED);
    st(m1,0x75,4096); assert(m2[4095]==0x75); char byte; assert(pread(fd,&byte,1,4095)==1 && byte==0x75);
    assert(!ftruncate(fd,0)); if(!sigsetjmp(jump,1)) { cp(b,m1,1); assert(0); }
    assert(fault_signal==SIGBUS && fault_address==(uintptr_t)m1);
    // Correctly encoded HLT outside the signed-in entry contract stays SIGILL.
    uint32_t malformed[]={0xd2800050,0xf2a00030,0xd2800031,0xd44a8a40,0xd65f03c0};
    cp(code,malformed,sizeof(malformed)); __builtin___clear_cache((char*)code,(char*)code+sizeof(malformed));
    if(!sigsetjmp(jump,1)) { ((void(*)(void))code)(); assert(0); }
    assert(fault_signal==SIGILL && fault_address==(uintptr_t)code+12);
    puts("accelerator-pure-ok"); return 0;
}
