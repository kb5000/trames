// SPDX-License-Identifier: GPL-3.0-or-later
// Link with --wrap=main to run an unchanged standard Vulkan fixture while a
// second guest thread retains its VM View. This exercises the worker transport
// even after the fixture's own asynchronous compiler peer has exited.
#include <assert.h>
#include <pthread.h>
extern int __real_main(int, char **);
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static int finished;
static void *peer(void *unused) {
    (void)unused;
    assert(!pthread_mutex_lock(&mutex));
    while (!finished) assert(!pthread_cond_wait(&changed,&mutex));
    assert(!pthread_mutex_unlock(&mutex));
    return 0;
}
int __wrap_main(int argc,char **argv) {
    pthread_t thread; assert(!pthread_create(&thread,0,peer,0));
    int result=__real_main(argc,argv);
    assert(!pthread_mutex_lock(&mutex)); finished=1;
    assert(!pthread_cond_signal(&changed)); assert(!pthread_mutex_unlock(&mutex));
    assert(!pthread_join(thread,0));
    return result;
}
