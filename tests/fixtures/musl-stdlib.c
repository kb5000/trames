// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
static unsigned id;
static void number(const char *s,int base) {
    char *end;errno=EDOM;long long a=strtoll(s,&end,base);size_t ae=end-s;int error=errno;
    errno=EDOM;long b=strtol(s,&end,base);assert(b==a && (size_t)(end-s)==ae && errno==error);
    errno=EDOM;intmax_t c=strtoimax(s,&end,base);assert(c==a && (size_t)(end-s)==ae && errno==error);
    errno=EDOM;unsigned long long u=strtoull(s,&end,base);size_t ue=end-s;int uerror=errno;
    errno=EDOM;unsigned long v=strtoul(s,&end,base);assert(v==u && (size_t)(end-s)==ue && errno==uerror);
    errno=EDOM;uintmax_t w=strtoumax(s,&end,base);assert(w==u && (size_t)(end-s)==ue && errno==uerror);
    printf("%u %d %016llx %zu %d %016llx %zu %d\n",id++,base,(unsigned long long)a,ae,error,u,ue,uerror);
}
static void copies(char *s,size_t len,char *d) {
    memset(d,17,len+145);assert(strcpy(d+1,s)==d+1);assert(!memcmp(d+1,s,len+1));assert(d[0]==17 && d[len+2]==17);
    memset(d,17,len+145);assert(stpcpy(d+1,s)==d+len+1);assert(!memcmp(d+1,s,len+1));assert(d[0]==17 && d[len+2]==17);
    memset(d,17,len+145);memcpy(d,"prefix",7);assert(strcat(d,s)==d);
    assert(!memcmp(d,"prefix",6) && !memcmp(d+6,s,len+1) && d[len+7]==17);
    size_t bounds[]={0,1,63,128,len,len+1,SIZE_MAX};
    for(unsigned k=0;k<sizeof(bounds)/sizeof(*bounds);k++){
        size_t n=bounds[k],used=len<n?len:n;memset(d,17,len+145);memcpy(d,"prefix",7);
        assert(strncat(d,s,n)==d);assert(!memcmp(d,"prefix",6) && !memcmp(d+6,s,used));
        assert(d[used+6]==0 && d[used+7]==17);
    }
    size_t sizes[]={0,1,63,64,127,128,129,len,len+1,len+8};
    for(unsigned k=0;k<sizeof(sizes)/sizeof(*sizes);k++){
        size_t n=sizes[k];for(int ending=0;ending<2;ending++){
            memset(d,17,len+145);char *p=ending?stpncpy(d+1,s,n):strncpy(d+1,s,n);
            assert(p==d+1+(ending?(len<n?len:n):0));size_t used=len<n?len:n;
            assert(!memcmp(d+1,s,used));for(size_t i=used;i<n;i++)assert(!d[i+1]);assert(d[0]==17 && d[n+1]==17);
        }
    }
}
static void strings(void) {
    for(unsigned align=0;align<8;align++) {
        char storage[96],*s=storage+align;memset(storage,1,sizeof(storage));
        for(unsigned i=0;i<61;i++)s[i]=(char)(i*37%255+1);s[61]=0;
        for(int c=0;c<256;c++) {
            char *first=0,*last=0;for(unsigned i=0;i<=61;i++)if((unsigned char)s[i]==c){if(!first)first=s+i;last=s+i;}
            assert(strchr(s,c)==first && strrchr(s,c)==last && strchrnul(s,c)==(first?first:s+61));
        }
    }
    size_t lengths[]={0,1,7,63,64,65,127,128,129,4095,4096,4097,65535,65536,70000};
    int chars[]={0,'a','z',255,511,-1};
    for(unsigned k=0;k<sizeof(lengths)/sizeof(*lengths);k++){
        size_t n=lengths[k];char *s=malloc(n+1),*d=malloc(n+145);assert(s && d);memset(s,'a',n);s[n]=0;
        if(n){s[n/2]=(char)255;s[n-1]='z';}
        for(unsigned j=0;j<sizeof(chars)/sizeof(*chars);j++){
            unsigned char c=chars[j];char *first=0,*last=0;
            for(size_t i=0;i<=n;i++)if((unsigned char)s[i]==c){if(!first)first=s+i;last=s+i;}
            assert(strchr(s,chars[j])==first && strrchr(s,chars[j])==last && strchrnul(s,chars[j])==(first?first:s+n));
        }
        copies(s,n,d);free(s);free(d);
    }
    char *s=mmap(0,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(s!=MAP_FAILED);assert(!mprotect(s+4096,4096,PROT_NONE));
    memset(s,'a',4096);s[4095]=0;char *d=malloc(8200);assert(d);copies(s,4095,d);assert(strchr(s,'a')==s && strrchr(s,'a')==s+4094 && strchrnul(s,'z')==s+4095);
    memset(s,'a',4096);assert(stpncpy(d,s,4096)==d+4096 && !memcmp(s,d,4096));free(d);assert(!munmap(s,8192));
    char *shared=mmap(0,8192,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(shared!=MAP_FAILED);
    char text[512];memset(text,'x',511);text[511]=0;assert(stpcpy(shared,text)==shared+511 && !strcmp(shared,text));assert(stpncpy(shared,"hi",512)==shared+2);assert(!munmap(shared,8192));
    char *writeonly=mmap(0,4096,PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(writeonly!=MAP_FAILED);
    assert(stpcpy(writeonly,text)==writeonly+511);assert(stpncpy(writeonly,"hi",512)==writeonly+2);
    assert(!mprotect(writeonly,4096,PROT_READ));assert(!strcmp(writeonly,"hi"));for(int i=3;i<512;i++)assert(!writeonly[i]);assert(!munmap(writeonly,4096));
}
static void *thread(void *ignored){(void)ignored;char *e;errno=EACCES;assert(strtoll("123!",&e,10)==123 && *e=='!' && errno==EACCES);assert(strtoull("18446744073709551616",0,10)==ULLONG_MAX && errno==ERANGE);return 0;}
static volatile sig_atomic_t handled;
static void handler(int sig){assert(sig==SIGUSR1);thread(0);handled++;}
static int cmp(const void *a,const void *b){long x=strtol(a,0,10),y=strtol(b,0,10);return (x>y)-(x<y);}
int main(int argc,char **argv){
    if(argc>1 && !strcmp(argv[1],"bench")){char s[64];unsigned long long total=0;for(int i=0;i<4096;i++){snprintf(s,64,"%d!",i);total+=strtoull(s,0,10);}assert(total==8386560);puts("stdlib-bench-ok");return 0;}
    if(argc>1 && !strcmp(argv[1],"strings-bench")){char a[8192],b[8192];memset(a,'x',8191);a[8191]=0;for(int i=0;i<256;i++){assert(stpcpy(b,a)==b+8191);assert(strchr(b,'z')==0);assert(strrchr(b,'x')==b+8190);}puts("strings-bench-ok");return 0;}
    const char *cases[]={""," ","+","-","+x","0","-0","0x","0x!","-0Xf!","09!","0b11","2147483648","9223372036854775807","9223372036854775808","-9223372036854775808","-9223372036854775809","18446744073709551615","18446744073709551616","-18446744073709551615","-18446744073709551616"," \t\n+123xyz","zzzzz!","\200123"};
    int bases[]={0,1,2,8,10,16,36,37,-1};for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++)for(unsigned j=0;j<sizeof(bases)/sizeof(*bases);j++)number(cases[i],bases[j]);
    uint32_t random=17;char text[128];for(int i=0;i<256;i++){random=random*1664525u+1013904223u;snprintf(text,128,"%c%08x%08x!",i&1?'-':'+',random,random^0x76543210u);number(text,i%35+2);}
    char *guard=mmap(0,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+4096,4096,PROT_NONE));memcpy(guard+4092,"123!",4);number(guard+4092,10);memcpy(guard+4092,"123",4);number(guard+4092,10);assert(!munmap(guard,8192));
    char *longtext=malloc(70016);assert(longtext);memset(longtext,' ',70000);memcpy(longtext+70000,"17!",4);number(longtext,10);memset(longtext,' ',4095);memcpy(longtext+4095,"0x7f!",6);number(longtext,0);free(longtext);
    char *end=(void *)17;errno=0;assert(!strtol(0,&end,1) && !end && errno==EINVAL);
    const char *aliases[]={"__strtol_internal","__strtoul_internal","__strtoll_internal","__strtoull_internal","__strtoimax_internal","__strtoumax_internal"};
    for(unsigned i=0;i<6;i++){long (*f)(const char *,char **,int,int)=dlsym(RTLD_DEFAULT,aliases[i]);assert(f);errno=EDOM;assert(f("17!",&end,10,0)==17 && *end=='!' && errno==EDOM);}
    pthread_t a,b;assert(!pthread_create(&a,0,thread,0));assert(!pthread_create(&b,0,thread,0));assert(!pthread_join(a,0));assert(!pthread_join(b,0));
    void *stack=mmap(0,131072,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(stack!=MAP_FAILED);pthread_attr_t attr;assert(!pthread_attr_init(&attr));assert(!pthread_attr_setstack(&attr,stack,131072));assert(!pthread_create(&a,&attr,thread,0));assert(!pthread_join(a,0));assert(!pthread_attr_destroy(&attr));assert(!munmap(stack,131072));
    struct sigaction action={.sa_handler=handler};sigemptyset(&action.sa_mask);assert(!sigaction(SIGUSR1,&action,0));assert(!raise(SIGUSR1));assert(handled==1);
    char rows[64][32];for(int i=0;i<64;i++)snprintf(rows[i],32,"%d!",63-i);qsort(rows,64,32,cmp);for(int i=0;i<64;i++)assert(strtol(rows[i],0,10)==i);
    if(!getenv("TRAMES_ORACLE_SKIP_EXEC")){pid_t child=fork();assert(child>=0);if(!child){thread(0);_exit(0);}int status;assert(waitpid(child,&status,0)==child && status==0);}
    strings();puts("musl-stdlib-ok");return 0;
}
