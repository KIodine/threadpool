#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include <sys/eventfd.h>

#include "list.h"
#include "gate.h"

#define MAX_WORKERS 128UL

/*
    - cleanup when thread terminates accidentally?
      -> just let them die, hiding no bug
*/

/*
    TODO:
    - [X] STRICT fail check on alloc
    - [X] use debug print (follow the `assert` disable method)
    - [X] use C style comment
    - [X] static and shared build makefile
*/

struct threadpool {
    unsigned int    n_workers;
    unsigned int    n_idle;
    unsigned int    is_paused;
    struct list     threadq;
    struct list     jobq;
    unsigned int    pending_jobs;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    struct gate    *wgate;
};

struct thread_ctx {
    pthread_t          tid;
    struct threadpool *pool;
    int                is_terminated;
    struct list        node;
};

struct jobinfo {
    void *(*func)(void*);
    void       *arg;
    struct list node;
};


struct threadpool *threadpool_alloc(void);
void threadpool_free(struct threadpool *pool);

void threadpool_wait(struct threadpool *pool);
int threadpool_submit(struct threadpool *pool, void *(*func)(void*), void *arg);

int threadpool_scale_to(struct threadpool *pool, unsigned int n);
int threadpool_pause(struct threadpool *pool);
int threadpool_resume(struct threadpool *pool);


#endif /* THREADPOOL_H */