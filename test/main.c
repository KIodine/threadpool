#define _GNU_SOURCE
// for `clock_*` series API and get vscode lint working

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>

#include "threadpool.h"

#define NL "\n"


//static const char *s = "[JOB] Hello <%2d> from %lX"NL;
static const struct timespec ts = {
    .tv_sec  = 0,
    .tv_nsec = 5000000
}; // 0.5 sec

/* Shared counter for debug. */
static int counter = 0;

static
void *job(void *arg){
    
    /* Mocking a time consuming job. */
    nanosleep(&ts, NULL);
    /* Just testing simple arithmetic op. */
    __sync_add_and_fetch(&counter, 1);

    return NULL;
}

static
void submit_n(struct threadpool *tp, void*(*fp)(void*), int n){
    assert(n >= 0);
    for (int i = 0; i < n; ++i){
        threadpool_submit(tp, fp, NULL);
    }
    return;
}

static
void basic_test(void){
    struct threadpool *tp = NULL;
    
    tp = threadpool_alloc();
    counter = 0;

    threadpool_scale_to(tp, 4UL);

    submit_n(tp, job, 32);
    threadpool_wait(tp);
    
    assert(counter == 32);
    
    submit_n(tp, job, 32);

    threadpool_pause(tp);
    threadpool_resume(tp);

    threadpool_resume(tp);

    submit_n(tp, job, 64);
    threadpool_wait(tp);

    assert(counter == 128);

    threadpool_free(tp);
    tp = NULL;
    return;
}

static
void basic_test2(void){
    struct threadpool *tp = NULL;
    uint64_t i = 0;
    counter = 0;

    tp = threadpool_alloc();
    threadpool_scale_to(tp, 4UL);

    submit_n(tp, job, 16);

    threadpool_wait(tp);
    assert(counter == 16);
    
    submit_n(tp, job, 16);

    threadpool_scale_to(tp, 8UL);
    threadpool_resume(tp);
    
    threadpool_scale_to(tp, 2UL);

    submit_n(tp, job, 16);

    threadpool_wait(tp);
    assert(counter == 48);

    threadpool_free(tp);
    tp = NULL;
    
    return;
}

int main(void){
    basic_test();
    printf("--- basic test 1 passed ---"NL);

    basic_test2();
    printf("--- basic test 2 passed ---"NL);
    return 0;
}