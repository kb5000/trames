// SPDX-License-Identifier: GPL-3.0-or-later
// Linux diagnostic preload: sample only the owning process, never guest state.
#define _GNU_SOURCE
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>
#define MAX_SAMPLES 200000UL
_Static_assert(ATOMIC_LONG_LOCK_FREE == 2, "signal sampler requires lock-free atomics");
static _Atomic unsigned long used;
static _Atomic unsigned long pcs[MAX_SAMPLES];
static unsigned long base, text_low, text_high;
static int active;
static pid_t owner;
static const char *destination;
static int executable(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size; (void)data;
    if (info->dlpi_name && info->dlpi_name[0]) return 0;
    base = info->dlpi_addr;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type == PT_LOAD && (p->p_flags & PF_X)) {
            unsigned long lo = base + p->p_vaddr, hi = lo + p->p_memsz;
            if (!text_low || lo < text_low) text_low = lo;
            if (hi > text_high) text_high = hi;
        }
    }
    return 1;
}
static void sample(int number, siginfo_t *info, void *context) {
    (void)number; (void)info;
    ucontext_t *uc = context;
#if defined(__x86_64__)
    unsigned long pc = uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
    unsigned long pc = uc->uc_mcontext.pc;
#else
#error Unsupported diagnostic host
#endif
    unsigned long i = atomic_fetch_add_explicit(&used, 1, memory_order_relaxed);
    if (i < MAX_SAMPLES)
        atomic_store_explicit(&pcs[i], pc >= text_low && pc < text_high ? pc - base : 0, memory_order_relaxed);
}
__attribute__((constructor)) static void begin(void) {
    destination = getenv("TRAMES_PC_SAMPLES");
    if (!destination || !*destination) return;
    // Storage initialization may exec host timezone helpers. Inherited preload
    // settings must not let those children create the runner's output first.
    const char *target = getenv("TRAMES_PC_EXECUTABLE");
    char executable_path[4096];
    ssize_t length = readlink("/proc/self/exe", executable_path, sizeof executable_path - 1);
    if (!target || length < 0) return;
    executable_path[length] = 0;
    if (strcmp(target, executable_path)) return;
    owner = getpid();
    struct sigaction old, action = {0};
    if (sigaction(SIGPROF, NULL, &old) || (old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN)) return;
    dl_iterate_phdr(executable, NULL);
    if (!text_low) return;
    action.sa_sigaction = sample;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPROF, &action, NULL)) return;
    // ITIMER_PROF is charged to this process only. Effective frequency depends
    // on host kernel ticks; percentages use actual samples, not requested Hz.
    struct itimerval timer = {{0, 1000}, {0, 1000}};
    if (!setitimer(ITIMER_PROF, &timer, NULL)) active = 1;
}
__attribute__((destructor)) static void finish(void) {
    if (!active || getpid() != owner) return;
    struct itimerval timer = {{0, 0}, {0, 0}};
    setitimer(ITIMER_PROF, &timer, NULL);
    struct sigaction ignore = {0}; ignore.sa_handler = SIG_IGN;
    sigaction(SIGPROF, &ignore, NULL);
    unsigned long n = atomic_load_explicit(&used, memory_order_relaxed);
    FILE *out = fopen(destination, "wx");
    if (!out) { perror("pc-sampler output"); return; }
    fprintf(out, "TRAMES-PC-SAMPLES-v1\nrequested_interval_us 1000\nsamples %lu\ndropped %lu\n", n < MAX_SAMPLES ? n : MAX_SAMPLES, n > MAX_SAMPLES ? n - MAX_SAMPLES : 0);
    for (unsigned long i = 0; i < n && i < MAX_SAMPLES; ++i)
        fprintf(out, "%lx\n", atomic_load_explicit(&pcs[i], memory_order_relaxed));
    fclose(out);
}
