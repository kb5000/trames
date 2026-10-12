// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
struct item { int key, id; unsigned char payload[249]; };
static uint64_t trace=17;
static unsigned calls;
static int plain(const void *a, const void *b) {
    int x=*(const int*)a,y=*(const int*)b;
    return (x>y)-(x<y);
}
static int traced(const void *a, const void *b, void *context) {
    const struct item *x=a,*y=b;
    assert(*(const int*)context==37);
    trace=trace*6364136223846793005ULL+(uint32_t)x->id*17u+(uint32_t)y->id;
    calls++;
    return (x->key>y->key)-(x->key<y->key);
}
static int descending(const void *a,const void *b,void *arg) { assert(*(int*)arg==-1);return -plain(a,b); }
static void make(int *p,size_t n) { for(size_t i=0;i<n;i++)p[i]=(int)((i*73+17)%101); }
static void sorted(const int *p,size_t n) { for(size_t i=1;i<n;i++)assert(p[i-1]<=p[i]); }
static _Thread_local int inside;
static _Thread_local unsigned nested_runs;
static int nested(const void *a, const void *b) {
    if(nested_runs++>=4)return plain(a,b);
    int inner[40],direction=-1;make(inner,40);
    assert(!inside);inside=1;
    qsort_r(inner,40,sizeof(*inner),descending,&direction);
    for(unsigned i=1;i<40;i++)assert(inner[i-1]>=inner[i]);
    qsort(inner,40,sizeof(*inner),plain);sorted(inner,40);
    int key=inner[17];assert(bsearch(&key,inner,40,sizeof(*inner),plain));
    char left[160],right[160];memset(left,'q',159);memset(right,'q',159);left[159]=right[159]=0;
    assert(!strcmp(left,right)); // guest comparator -> another native profile.
    inside=0;return plain(a,b);
}
static void *thread(void *unused) { (void)unused;int a[48];make(a,48);qsort(a,48,4,nested);sorted(a,48);return 0; }
static void *shared_stack_thread(void *unused) {
    (void)unused;int *a=malloc(128*4);assert(a);make(a,128);
    // The heap array is private; only the callback continuation stack is shared.
    qsort(a,128,4,plain);sorted(a,128);free(a);return 0;
}
static sigjmp_buf escape;
static int jump_count;
static int jumping(const void *a,const void *b) { (void)a;(void)b;if(++jump_count==7)siglongjmp(escape,1);return plain(a,b); }
static volatile sig_atomic_t signals_seen;
static void handler(int sig) { assert(sig==SIGUSR1);int a[40];make(a,40);qsort(a,40,4,plain);sorted(a,40);signals_seen++; }
static int signalled;
static int signalling(const void *a,const void *b) { if(!signalled){signalled=1;assert(!raise(SIGUSR1));}return plain(a,b); }
static pid_t fork_child;
static int forked;
static int forking(const void *a,const void *b) { if(!forked){forked=1;fork_child=fork();assert(fork_child>=0);}return plain(a,b); }
static int io_once;
static int with_io(const void *a,const void *b) {
    if(!io_once){io_once=1;int fd[2];assert(!pipe(fd));pid_t child=fork();assert(child>=0);
        if(!child){close(fd[0]);usleep(2000);assert(write(fd[1],"x",1)==1);_exit(0);}
        close(fd[1]);char value;assert(read(fd[0],&value,1)==1&&value=='x');close(fd[0]);int status;assert(waitpid(child,&status,0)==child&&status==0);}
    return plain(a,b);
}
static int pointer_search(const void *key,const void *element) { assert(key==(void*)1);assert(element==(void*)2);return 0; }
int main(int argc,char **argv) {
    if(argc>1 && !strncmp(argv[1],"exec-",5)) {
        assert((getauxval(0x54520004UL)==1)==(!strcmp(argv[1],"exec-search")));
        int a[48];make(a,48);qsort(a,48,4,plain);sorted(a,48);int key=a[17];assert(bsearch(&key,a,48,4,plain));return 0;
    }
    if(argc>1){
        size_t n=(size_t)strtoul(argv[1],0,10);assert(n>=32&&n<=8192);int *a=malloc(n*4);assert(a);
        for(int i=0;i<4;i++){make(a,n);qsort(a,n,4,plain);sorted(a,n);}puts("callbacks-bench-ok");return 0;
    }
    int context=37;
    const unsigned sizes[]={0,1,2,3,7,15,31,32,33,63,64,65,127,128,180};
    for(unsigned size=0;size<sizeof(sizes)/sizeof(*sizes);size++) {
        unsigned n=sizes[size];
        struct item *a=malloc((n+1)*sizeof(*a));assert(a);
        for(unsigned i=0;i<n;i++){a[i].key=(int)((i*73+17)%13);a[i].id=(int)i;memset(a[i].payload,i,249);}
        qsort_r(a,n,sizeof(*a),traced,&context);
        unsigned seen[180]={0};
        for(unsigned i=0;i<n;i++){if(i)assert(a[i-1].key<=a[i].key);assert((unsigned)a[i].id<n);seen[a[i].id]++;for(unsigned j=0;j<249;j++)assert(a[i].payload[j]==(unsigned char)a[i].id);}
        for(unsigned i=0;i<n;i++)assert(seen[i]==1);free(a);
    }
    int a[128];make(a,128);errno=EDOM;qsort(a,128,4,nested);sorted(a,128);assert(errno==EDOM);
    for(int key=-1;key<103;key++) {int *found=bsearch(&key,a,128,4,plain);int present=0;for(unsigned i=0;i<128;i++)present|=a[i]==key;assert((found!=0)==present);if(found)assert(*found==key);}
    qsort((void*)1,0,4,plain);qsort((void*)1,5,0,plain);assert(!bsearch((void*)1,(void*)2,0,4,plain));
    assert(bsearch((void*)1,(void*)2,32,0,pointer_search)==(void*)2);
    // Bound fallback and live shared backing retain original musl semantics.
    int *large=malloc(70000);assert(large);make(large,17500);qsort(large,17500,4,plain);sorted(large,17500);free(large);
    int *shared=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(shared!=MAP_FAILED);make(shared,128);qsort(shared,128,4,nested);sorted(shared,128);
    int *readonly=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(readonly!=MAP_FAILED);for(int i=0;i<128;i++)readonly[i]=i;
    assert(!mprotect(readonly,4096,PROT_READ));qsort(readonly,128,4,plain);sorted(readonly,128);
    // A nonlocal exit discards the guest continuation, leaving no host operation.
    make(a,128);if(!sigsetjmp(escape,1)){qsort(a,128,4,jumping);assert(0);}assert(jump_count==7);make(a,128);qsort(a,128,4,plain);sorted(a,128);
    stack_t alternate={.ss_sp=malloc(32768),.ss_size=32768};assert(alternate.ss_sp&&!sigaltstack(&alternate,0));
    struct sigaction action={.sa_handler=handler,.sa_flags=SA_ONSTACK};sigemptyset(&action.sa_mask);assert(!sigaction(SIGUSR1,&action,0));
    make(a,128);qsort(a,128,4,signalling);sorted(a,128);assert(signals_seen==1);
    // Both parent and child finish the same guest stack continuation after fork.
    if(!getenv("TRAMES_ORACLE_SKIP_EXEC")) {
        make(a,128);qsort(a,128,4,forking);sorted(a,128);
        if(fork_child==0)_exit(0);int status;assert(waitpid(fork_child,&status,0)==fork_child&&status==0);
        make(a,128);qsort(a,128,4,with_io);sorted(a,128);
        pid_t child=fork();assert(child>=0);
        if(!child){execl("/bin/accelerator-test","accelerator-test",getauxval(0x54520004UL)==1 ? "exec-search" : "exec-local",(char*)0);_exit(99);}
        assert(waitpid(child,&status,0)==child&&status==0);
    }
    pthread_t first,second;assert(!pthread_create(&first,0,thread,0));assert(!pthread_create(&second,0,thread,0));assert(!pthread_join(first,0));assert(!pthread_join(second,0));
    void *shared_stack=mmap(0,131072,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(shared_stack!=MAP_FAILED);
    pthread_attr_t attr;assert(!pthread_attr_init(&attr));assert(!pthread_attr_setstack(&attr,shared_stack,131072));
    assert(!pthread_create(&first,&attr,shared_stack_thread,0));assert(!pthread_join(first,0));assert(!pthread_attr_destroy(&attr));assert(!munmap(shared_stack,131072));
    printf("musl-callbacks-ok %u %016llx\n",calls,(unsigned long long)trace);
    return 0;
}
