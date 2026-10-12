// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
static __thread unsigned tls=23;
static unsigned constructor_ok;
__attribute__((constructor)) static void initialize(void) {
    unsigned char a[8192],b[8192]; memset(a,0x42,sizeof a); memcpy(b,a,sizeof a);
    assert(!memcmp(a,b,sizeof a)); assert(tls==23); tls=29; constructor_ok=1;
}
static void *worker(void *unused) {
    (void)unused; assert(tls==23); tls=41;
    for (size_t n=1;n<20000;n=n*2+1) {
        unsigned char *p=malloc(n),*q=calloc(n,1); assert(p&&q);
        memset(p,0x81,n); assert(!q[n-1]); q=realloc(q,n+1); assert(q);
        memcpy(q,p,n); assert(!memcmp(p,q,n)); free(p); free(q);
    }
    errno=ERANGE; char a[4096],b[4096]; memset(a,'s',sizeof a); memcpy(b,a,sizeof a);
    assert(errno==ERANGE); return NULL;
}
int main(void) {
    assert(constructor_ok && tls==29 && getauxval(AT_BASE));
    void *h=dlopen("/test/libm3probe.so",RTLD_NOW); assert(h);
    long (*probe)(long)=dlsym(h,"probe"); assert(probe && probe(5)==12);
    pthread_t thread; assert(!pthread_create(&thread,NULL,worker,NULL));
    assert(!pthread_join(thread,NULL) && tls==29 && probe(3)==15);
    assert(!dlclose(h));
    assert(!dlsym(RTLD_DEFAULT,"trames_orig_memcpy"));
    assert(!dlsym(RTLD_DEFAULT,"trames_call_1"));
    volatile double value=81; assert(sqrt(value)==9);
    FILE *f=tmpfile(); assert(f); assert(fprintf(f,"%s:%d:%.2f","musl",42,1.25)==12);
    rewind(f); char text[32]; assert(fgets(text,sizeof text,f)); assert(!strcmp(text,"musl:42:1.25")); fclose(f);
    puts("musl-startup-ok"); return 0;
}
