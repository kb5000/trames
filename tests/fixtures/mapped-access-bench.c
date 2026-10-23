// SPDX-License-Identifier: GPL-3.0-or-later
// Fixed-work diagnostic for per-access mapped-page overhead, not a score.
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc, char **argv) {
    const uint64_t n=5000000;
    int shared=argc==2 && !strcmp(argv[1],"memfd");
    mkdir("/tmp",0700);
    int fd=shared?memfd_create("mapped-loop",0):open("/tmp/mapped-loop",O_RDWR|O_CREAT|O_TRUNC,0600);
    assert(fd>=0 && ftruncate(fd,4096)==0);
    volatile uint64_t *p=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    assert((void *)p!=MAP_FAILED);
    uint64_t sum=0;
    for(uint64_t i=0;i<n;++i) { unsigned slot=(unsigned)i&511; p[slot]=i; sum+=p[slot]; }
    assert(sum==n*(n-1)/2);
    assert(msync((void *)p,4096,MS_SYNC)==0);
    uint64_t last=0; assert(pread(fd,&last,8,((n-1)&511)*8)==8 && last==n-1);
    assert(munmap((void *)p,4096)==0 && close(fd)==0);
    puts("mapped-loop: checksum and backing: ok");
    return 0;
}
