// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static int value;
static atomic_int fired;
static atomic_int ready;
static int shared_fd;
static pid_t leader;
static void *worker(void *unused) {
    (void)unused;
    pthread_mutex_lock(&mutex);
    if (getpid() != leader || gettid() == leader) return (void *)1;
    atomic_store(&ready, 1);
    while (!value) pthread_cond_wait(&condition, &mutex);
    value += 41;
    close(shared_fd);
    pthread_mutex_unlock(&mutex);
    return (void *)42;
}
static void callback(union sigval argument) {
    atomic_store(&fired, argument.sival_int);
}
static int threads(void) {
    pthread_t thread;
    int descriptors[2];
    if (pipe(descriptors)) return 1;
    shared_fd = descriptors[0]; leader = getpid();
    int error = pthread_create(&thread, NULL, worker, NULL);
    if (error) { fprintf(stderr, "pthread_create: %s\n", strerror(error)); return 1; }
    for (int i=0; i<1000 && !atomic_load(&ready); i++) {
        struct timespec delay={.tv_nsec=1000000}; nanosleep(&delay,NULL);
    }
    if (!atomic_load(&ready)) return 3;
    pthread_mutex_lock(&mutex);
    value = 1;
    pthread_cond_signal(&condition);
    pthread_mutex_unlock(&mutex);
    void *result;
    if (pthread_join(thread, &result) || result != (void *)42 || value != 42) return 2;
    char byte;
    if (read(shared_fd, &byte, 1) != -1 || errno != EBADF) return 4;
    close(descriptors[1]);
    puts("pthread shared memory, mutex, condition and join: OK");
    return 0;
}
static int timers(void) {
    struct sigevent event = { .sigev_notify = SIGEV_THREAD, .sigev_notify_function = callback, .sigev_value.sival_int = 42 };
    timer_t timer;
    if (timer_create(CLOCK_MONOTONIC, &event, &timer)) { perror("timer_create"); return 1; }
    struct itimerspec setting = { .it_value.tv_nsec = 10000000 };
    if (timer_settime(timer, 0, &setting, NULL)) { perror("timer_settime"); return 2; }
    for (int i = 0; i < 100 && !atomic_load(&fired); i++) {
        struct timespec delay = { .tv_nsec = 1000000 }; nanosleep(&delay, NULL);
    }
    if (atomic_load(&fired) != 42) return 3;
    if (timer_delete(timer)) { perror("timer_delete"); return 4; }
    puts("POSIX SIGEV_THREAD monotonic timer callback: OK");
    return 0;
}
int main(int argc, char **argv) {
    if (argc != 2) return 64;
    if (!strcmp(argv[1], "threads")) return threads();
    if (!strcmp(argv[1], "timers")) return timers();
    return 64;
}
