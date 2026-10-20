// SPDX-License-Identifier: GPL-3.0-or-later
// Original-musl ABI regression: Gecko mixes coarse and precise monotonic time.
#define _GNU_SOURCE
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void require(int condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static int64_t nanos(struct timespec time) {
    require(time.tv_nsec >= 0 && time.tv_nsec < 1000000000, "normalized timespec");
    return (int64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}
static void same_epoch(clockid_t precise, clockid_t coarse) {
    struct timespec before, value, after, resolution;
    require(clock_gettime(precise, &before) == 0, "precise clock");
    require(clock_gettime(coarse, &value) == 0, "coarse clock");
    require(clock_gettime(precise, &after) == 0, "precise clock after");
    require(clock_getres(coarse, &resolution) == 0, "coarse resolution");
    require(nanos(resolution) > 0 && nanos(resolution) <= 1000000000, "resolution range");
    // Linux's native coarse clock may lag by a scheduler tick; permit that,
    // while rejecting Unix-vs-monotonic epoch errors by many orders of magnitude.
    require(nanos(value) >= nanos(before) - 2000000000LL &&
            nanos(value) <= nanos(after) + 2000000000LL, "coarse/precise epoch");
}
static void *clocks(void *unused) {
    (void)unused;
    for (int i = 0; i < 100; ++i) {
        same_epoch(CLOCK_REALTIME, CLOCK_REALTIME_COARSE);
        same_epoch(CLOCK_MONOTONIC, CLOCK_MONOTONIC_COARSE);
    }
    return NULL;
}
int main(void) {
    pthread_t peer;
    require(pthread_create(&peer, NULL, clocks, NULL) == 0, "create clock peer");
    clocks(NULL);
    require(pthread_join(peer, NULL) == 0, "join clock peer");
    puts("clock ABI: two threads, realtime/coarse and monotonic/coarse epochs PASS");
    return 0;
}
