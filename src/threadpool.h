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
    eventfd_t       no_working;
};
/*
    the eventfd saved one independant conditional lock and provided
    some important attribute -- a wake mechanism without yielding control
    flow and can be waited (though pipe might do the same thing).
    --> equivalent to a waitable(or say, blocking) one/low capacity queue
    but there is several questions -- does it saves more space compare
    to using the lock? and does it faster?
    if so, how much? -> find out the size of lock, cond lock and eventfd

    On other platform, one might use conditional lock to simulate eventfd.
*/

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