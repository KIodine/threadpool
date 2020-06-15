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
    
    nanosleep(&ts, NULL);
    __sync_add_and_fetch(&counter, 1);

    return NULL;
}

static
void basic_test(void){
    struct threadpool *tp = NULL;
    
    tp = threadpool_alloc();
    counter = 0;

    threadpool_scale_to(tp, 4UL);

    for (uint64_t i = 0; i < 32; ++i){
        threadpool_submit(tp, job, (void*)i);
    }
    threadpool_pause(tp);
    assert(counter == 32);

    
    for (uint64_t i = 32; i < 64; ++i){
        threadpool_submit(tp, job, (void*)i);
    }
    threadpool_resume(tp);

    for (uint64_t i = 0; i < 32; ++i){
        threadpool_submit(tp, job, (void*)i);
    }
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

    for (;i < 16; ++i){
        threadpool_submit(tp, job, (void*)i);
    }

    threadpool_pause(tp);
    assert(counter == 16);
    
    for (;i < 32; ++i){
        threadpool_submit(tp, job, (void*)i);
    }
    threadpool_scale_to(tp, 8UL);
    threadpool_resume(tp);
    
    threadpool_scale_to(tp, 2UL);

    for (;i < 48; ++i){
        threadpool_submit(tp, job, (void*)i);
    }
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