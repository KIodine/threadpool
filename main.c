#define _GNU_SOURCE
// for `clock_*` series API and get vscode lint working

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>

#include "src/threadpool.h"

#define NL "\n"


static const char *s = "[JOB] Hello <%2d> from %lX"NL;
static const struct timespec ts = {
    .tv_sec  = 0,
    .tv_nsec = 5000000
}; // 0.5 sec

static
void *job(void *arg){
    pthread_t selfid;
    uint64_t i = (uint64_t)arg;
    
    selfid = pthread_self();
    printf(s, i, selfid);
    nanosleep(&ts, NULL);
    printf("[JOB] job done from %lX"NL, selfid);

    return NULL;
}

static
void basic_test(void){
    struct threadpool *tp = NULL;
    tp = threadpool_alloc();

    threadpool_scale_to(tp, 4UL);

    for (uint64_t i = 0; i < 32; ++i){
        threadpool_submit(tp, job, (void*)i);
    }

    threadpool_pause(tp);
    for (uint64_t i = 32; i < 64; ++i){
        threadpool_submit(tp, job, (void*)i);
    }
    threadpool_resume(tp);

    for (uint64_t i = 0; i < 32; ++i){
        threadpool_submit(tp, job, (void*)i);
    }
    threadpool_wait(tp);

    threadpool_free(tp);
    tp = NULL;
    return;
}

static
void basic_test2(void){
    /*
        alloc pool
        scale to 4
        submit jobs
        scale to 8
        pause
        submit jobs
        resume
        scale to 4
        submit jobs
        scale to 2
        wait
        dealloc pool
    */
    struct threadpool *tp = NULL;
    uint64_t i = 0;

    tp = threadpool_alloc();
    threadpool_scale_to(tp, 4UL);

    for (;i < 16; ++i){
        threadpool_submit(tp, job, (void*)i);
    }

    threadpool_pause(tp);
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
    threadpool_free(tp);
    
    tp = NULL;
    
    return;
}

int main(void){
    printf("--- begin basic test ---"NL);
    basic_test();
    
    printf("--- begin basic test 2 ---"NL);
    basic_test2();
    return 0;
}