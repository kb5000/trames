// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <locale.h>
#include <math.h>
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
static void check(const char *s, int round, locale_t locale) {
    char *end; errno=EDOM; double d=strtod(s,&end); int de=errno;size_t dn=end-s;
    errno=EDOM; float f=strtof(s,&end); int fe=errno;size_t fn=end-s;
    union {double value;uint64_t bits;} db={.value=d};union{float value;uint32_t bits;} fb={.value=f};
    printf("%u %d %016llx %zu %d %08x %zu %d\n",id++,round,(unsigned long long)db.bits,dn,de,fb.bits,fn,fe);
    errno=EDOM;double a=atof(s);assert((isnan(a)&&isnan(d)) || a==d);assert(errno==de);
    errno=EDOM;a=strtod_l(s,&end,locale);assert((isnan(a)&&isnan(d)) || a==d);assert(errno==de && (size_t)(end-s)==dn);
    errno=EDOM;float b=strtof_l(s,&end,locale);assert((isnan(b)&&isnan(f)) || b==f);assert(errno==fe && (size_t)(end-s)==fn);
}
static int comparator(const void *a,const void *b) {
    double x=strtod(a,0),y=strtod(b,0);return (x>y)-(x<y);
}
static void sort_numbers(void) {
    char rows[64][32];for(int i=0;i<64;i++)snprintf(rows[i],32,"%d.25",63-i);
    qsort(rows,64,32,comparator);
    for(int i=0;i<64;i++)assert(strtod(rows[i],0)==i+0.25);
}
static void *thread(void *ignored) {
    (void)ignored;errno=EACCES;char *end;
    assert(strtod("2.5!",&end)==2.5 && *end=='!' && errno==EACCES);
    assert(strtof("1e999",0)==INFINITY && errno==ERANGE);
    sort_numbers();return 0;
}
static volatile sig_atomic_t handled;
static void handler(int sig){assert(sig==SIGUSR1);char *end;assert(strtod("0x1.8p+2!",&end)==6 && *end=='!');handled++;}
int main(int argc,char **argv) {
    if(argc>1 && !strcmp(argv[1],"bench")) {
        char s[64];double total=0;for(int i=0;i<4096;i++){snprintf(s,64,"%d.125",i);total+=strtod(s,0);}
        assert(total==8387072.0);puts("numeric-bench-ok");return 0;
    }
    locale_t locale=newlocale(LC_ALL_MASK,"C",0);assert(locale);
    const char *cases[]={""," ","+","-",".","x","12x","-0","+0.0","1e","1e+","1e-z","0x","-0x.","0x1p","0x1p+", "0x1.8p+3!", "inf", "infinity", "INFINITY!", "infi", "nan", "-NaN", "nan()", "nan(payload_3)", "nan(a!", "nan(", "Na", "Infinitymore", "\t\n-12.5x", "1.0000000000000001", "1.0000000000000002", "1.7976931348623157e308", "1.7976931348623159e308", "2.2250738585072014e-308", "2.2250738585072011e-308", "4.9406564584124654e-324", "2.4703282292062327e-324", "1e-9999", "-1e9999", "3.4028234663852886e38", "3.4028235677973366e38", "1.1754943508222875e-38", "1.401298464324817e-45", "0x1p-149", "0x1p-1074", "0x1.fffffffffffffp1023", "\20012", "0.000000000000000000000000000000000000000000125"};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++)check(cases[i],0,locale);
    uint32_t random=17;char s[128];
    for(int i=0;i<512;i++){
        random=random*1664525u+1013904223u;
        snprintf(s,sizeof(s),"%c%u.%09ue%+d!",i&1?'-':'+',random,random^0x76543210u,i%661-330);check(s,0,locale);
    }
    for(int mode=0;mode<3;mode++){
        int modes[]={FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};assert(!fesetround(modes[mode]));
        for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++)check(cases[i],mode+1,locale);
    }
    assert(!fesetround(FE_TONEAREST));
    // A delimiter or NUL ends parsing before an unmapped following page.
    char *guard=mmap(0,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(guard!=MAP_FAILED);assert(!mprotect(guard+4096,4096,PROT_NONE));
    memcpy(guard+4091,"12.5x",5);check(guard+4091,0,locale);memcpy(guard+4091,"-0.5",5);check(guard+4091,0,locale);
    // Parsed prefixes can cross pages; long strings select original pre-write fallback.
    char *big=malloc(70016);assert(big);memset(big,' ',70000);memcpy(big+70000,"1.25!",6);check(big,0,locale);
    memset(big,' ',4095);memcpy(big+4095,"0x1.8p+2!",10);check(big,0,locale);free(big);
    pthread_t a,b;assert(!pthread_create(&a,0,thread,0));assert(!pthread_create(&b,0,thread,0));assert(!pthread_join(a,0));assert(!pthread_join(b,0));
    // Both sort and conversion must also work from a shared user-supplied stack.
    void *stack=mmap(0,131072,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(stack!=MAP_FAILED);
    pthread_attr_t attr;assert(!pthread_attr_init(&attr));assert(!pthread_attr_setstack(&attr,stack,131072));assert(!pthread_create(&a,&attr,thread,0));assert(!pthread_join(a,0));assert(!pthread_attr_destroy(&attr));assert(!munmap(stack,131072));
    struct sigaction action={.sa_handler=handler};sigemptyset(&action.sa_mask);assert(!sigaction(SIGUSR1,&action,0));assert(!raise(SIGUSR1));assert(handled==1);sort_numbers();
    if(!getenv("TRAMES_ORACLE_SKIP_EXEC")){
        pid_t child=fork();assert(child>=0);if(!child){thread(0);_exit(0);}int status;assert(waitpid(child,&status,0)==child && status==0);
    }
    char *end;long double wide=strtold("0x1.0000000000000000000000000001p+0!",&end);
    assert(wide==1.0L+0x1p-112L && *end=='!');freelocale(locale);puts("musl-numeric-ok");return 0;
}
