// SPDX-License-Identifier: GPL-3.0-or-later
// Optional Linux diagnostic for the loaded production FFI, never guest state.
#define _GNU_SOURCE
#include <link.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#define CAPACITY 200000
_Static_assert(ATOMIC_LONG_LOCK_FREE == 2, "signal samples require lock-free stores");
struct record { unsigned long pc, absolute_pc, tid, ns; _Atomic unsigned long ready; };
static struct record records[CAPACITY];
static _Atomic unsigned long count;
static unsigned long base, low, high;
static const char *module;
static char destination[4096];
static struct timespec began;
static struct sigaction previous;
static int active;
static int locate(struct dl_phdr_info *info, size_t size, void *unused) {
    (void)size; (void)unused;
    if (!info->dlpi_name || strcmp(info->dlpi_name, module)) return 0;
    base = info->dlpi_addr;
    for (int i=0;i<info->dlpi_phnum;i++) {
        const ElfW(Phdr) *p=&info->dlpi_phdr[i];
        if (p->p_type==PT_LOAD && (p->p_flags&PF_X)) {
            unsigned long begin=base+p->p_vaddr,end=begin+p->p_memsz;
            if (!low || begin<low) low=begin;
            if (end>high) high=end;
        }
    }
    return 1;
}
static void sample(int signal, siginfo_t *info, void *context) {
    (void)signal; (void)info;
    ucontext_t *uc=context;
#if defined(__x86_64__)
    unsigned long pc=uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
    unsigned long pc=uc->uc_mcontext.pc;
#else
#error unsupported diagnostic host
#endif
    unsigned long i=atomic_fetch_add_explicit(&count,1,memory_order_relaxed);
    if (i<CAPACITY) {
        struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
        records[i].pc=pc>=low&&pc<high?pc-base:0;
        records[i].absolute_pc=pc;
        records[i].tid=(unsigned long)syscall(SYS_gettid);
        records[i].ns=(now.tv_sec-began.tv_sec)*1000000000UL+now.tv_nsec-began.tv_nsec;
        atomic_store_explicit(&records[i].ready,1,memory_order_release);
    }
}
int trames_sample_start(const char *library,const char *output) {
    if (active || strlen(output)>=sizeof destination) return -1;
    module=library;strcpy(destination,output);
    dl_iterate_phdr(locate,NULL);
    if (!low || sigaction(SIGPROF,NULL,&previous) ||
        (previous.sa_handler!=SIG_DFL && previous.sa_handler!=SIG_IGN)) return -1;
    struct sigaction action={0};action.sa_sigaction=sample;action.sa_flags=SA_SIGINFO|SA_RESTART;
    sigemptyset(&action.sa_mask);
    clock_gettime(CLOCK_MONOTONIC,&began);
    if (sigaction(SIGPROF,&action,NULL)) return -1;
    struct itimerval timer={{0,1000},{0,1000}};
    if (setitimer(ITIMER_PROF,&timer,NULL)) {sigaction(SIGPROF,&previous,NULL);return -1;}
    active=1;return 0;
}
void trames_sample_stop(void) {
    if (!active) return;
    struct itimerval timer={{0,0},{0,0}};setitimer(ITIMER_PROF,&timer,NULL);
    // Do not reuse this test process for another run. SIG_IGN also drains any
    // pending profiling signal before writing the completed per-thread records.
    struct sigaction ignore={0};ignore.sa_handler=SIG_IGN;sigaction(SIGPROF,&ignore,NULL);
    FILE *file=fopen(destination,"wx");if (!file) return;
    unsigned long n=atomic_load_explicit(&count,memory_order_relaxed);
    fprintf(file,"TRAMES-SESSION-PC-v2\nsamples %lu dropped %lu\n",n<CAPACITY?n:CAPACITY,n>CAPACITY?n-CAPACITY:0);
    for(unsigned long i=0;i<n&&i<CAPACITY;i++) {
        // A signal may still be returning on another thread. Only read fully
        // published records; incomplete final samples are explicitly omitted.
        if (atomic_load_explicit(&records[i].ready,memory_order_acquire))
            fprintf(file,"%lu %lu %lx %lx\n",records[i].ns,records[i].tid,records[i].pc,records[i].absolute_pc);
    }
    fclose(file);active=0;
}
