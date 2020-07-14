#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>

#include "list.h"
#include "gate.h"

#include "threadpool.h"
#include "sthp_dbg.h"


/* --- public --- */
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

/* --- internal --- */
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


static void *worker(void* arg);
static unsigned int upscale(struct threadpool *pool, unsigned int n);
static unsigned int downscale(struct threadpool *pool, unsigned int n);


static
void *worker(void *arg){
    struct thread_ctx *tctx;
    struct threadpool *pool;
    pthread_mutex_t *lock;
    pthread_cond_t  *cond;
    struct jobinfo *job   = NULL;
    struct list *tmp_node = NULL;
    struct list *jobhead  = NULL;
    int was_working  = 0;
    
    assert(arg != NULL);
    
    /* avoid double pointer access */
    tctx    = (struct thread_ctx*)arg;
    pool    = tctx->pool;
    lock    = &pool->lock;
    cond    = &pool->cond;
    jobhead = &pool->jobq;

    debug_printf("[%lX] is ready"NL, tctx->tid);
    for (;;){
        pthread_mutex_lock(lock);
        debug_printf("[%lX] main lock acquired"NL, tctx->tid);
        if (was_working){
            pool->n_idle++;
            was_working = 0;
        }
        for (;;){
            if (tctx->is_terminated){
                /* if we're not "pop"ed, there is at least one node we
                    connect to: the head node. */
                if (list_is_empty(&tctx->node)){
                    /* we're poped from `threadq`, now the thread
                        handles itself. */
                    debug_printf("[\x1b[1;31m%lX\x1b[0m] being downscaled"NL, tctx->tid);
                    free(tctx);
                } else {
                    debug_printf("[%lX] terminated"NL, tctx->tid);
                }
                pthread_mutex_unlock(lock);
                goto end;
            }
            if (!list_is_empty(jobhead) && !pool->is_paused){
                tmp_node = list_get(jobhead);
                job = list_entry(tmp_node, struct jobinfo, node);
                pool->n_idle--;
                pool->pending_jobs--;
                was_working = 1;
                debug_printf("[%lX] get to work"NL, tctx->tid);
                pthread_mutex_unlock(lock);
                break;
            } else {
                /*  Gate is unlocked if workers spawn before any job
                    submitted. */
                if (pool->n_idle == pool->n_workers){
                    /* the last worker here would unlock the gate,
                        letting the waiters go. */
                    debug_printf("[%lX] last worker gate open"NL, tctx->tid);
                    /* Open the waiting "gate". */
                    gate_unlock(pool->wgate);
                }
            }
            debug_printf("[%lX] no job to do"NL, tctx->tid);
            pthread_cond_wait(cond, lock);
        }
        job->func(job->arg);
        free(job);
    }
end:
    pthread_exit(NULL);
}


static
unsigned int upscale(struct threadpool *pool, unsigned int n){
    struct thread_ctx *tmp;
    unsigned int created = 0;
    int err = 0;

    debug_printf("upscale by %d"NL, n);
    
    pthread_mutex_lock(&pool->lock);
    for (; created < n; ++created){
        tmp = calloc(1, sizeof(struct thread_ctx));
        tmp->pool = pool;
        list_push(&pool->threadq, &tmp->node);
        err = pthread_create(&tmp->tid, NULL, worker, tmp);
        if (err != 0){
            errno = err;
            perror("threadpool upscaling");
            break;
        }
    }
    /* they are initially treated as idle */
    pool->n_workers += created;
    pool->n_idle    += created;
    pthread_mutex_unlock(&pool->lock);
    return created;
}

static
unsigned int downscale(struct threadpool *pool, unsigned int n){
    struct thread_ctx *tmp_ctx;
    struct list *tmp_node;
    unsigned int detached = 0;

    debug_printf("downscale by %d"NL, n);

    pthread_mutex_lock(&pool->lock);
    for (; detached < n; ++detached){
        tmp_node = list_get(&pool->threadq);
        tmp_ctx  = list_entry(tmp_node, struct thread_ctx, node);
        tmp_ctx->is_terminated = 1;
        pthread_detach(tmp_ctx->tid);
    }
    pool->n_workers -= detached;
    pool->n_idle    -= detached;
    if (!pool->is_paused){
        pthread_cond_broadcast(&pool->cond);
    }
    pthread_mutex_unlock(&pool->lock);
    return detached;
}


struct threadpool *threadpool_alloc(void){
    struct threadpool *pool;
    struct gate *gate;
    int ret;
    
    /* always returns a "valid" pointer on Linux, until killed by oom */
    pool = calloc(1, sizeof(struct threadpool));
    list_init(pool->threadq);
    list_init(pool->jobq);

    gate = gate_alloc();
    if (gate == NULL){
        perror("Create gate");
        goto err_gate;
    }
    pool->wgate = gate;
    
    ret = pthread_mutex_init(&pool->lock, NULL);
    if (ret != 0){
        errno = ret;
        perror("Create mutex");
        goto err_lock;
    }

    ret = pthread_cond_init(&pool->cond, NULL);
    if (ret != 0){
        errno = ret;
        perror("Create cond");
        goto err_cond;
    }

    return pool;
err_cond:
    pthread_mutex_destroy(&pool->lock);
err_lock:
    gate_free(pool->wgate);
err_gate:
    free(pool);
    return NULL;
}

void threadpool_wait(struct threadpool *pool){

    pthread_mutex_lock(&pool->lock);
    if (pool->is_paused){
        pthread_mutex_unlock(&pool->lock);
        return;
    }
    pthread_mutex_unlock(&pool->lock);

    debug_printf("waiting for jobs complete"NL);

    /* Mainthread. */
    /* Wait until last worker opens the waiting "gate". */
    gate_wait(pool->wgate);
    
    assert(pool->pending_jobs == 0);

    return;
}

int threadpool_pause(struct threadpool *pool){

    pthread_mutex_lock(&pool->lock);
    if (pool->n_workers == 0 || pool->is_paused){
        pthread_mutex_unlock(&pool->lock);
        return 0;
    }
    pool->is_paused = 1;
    pthread_mutex_unlock(&pool->lock);

    /* Wait for the last worker opens the "gate". */
    gate_wait(pool->wgate);

    debug_printf("start pause"NL);

    return 0;
}

int threadpool_resume(struct threadpool *pool){

    pthread_mutex_lock(&pool->lock);
    if (pool->n_workers == 0 || !pool->is_paused){
        pthread_mutex_unlock(&pool->lock);
    }
    pool->is_paused = 0;

    gate_lock(pool->wgate);
    
    pthread_cond_broadcast(&pool->cond);
    
    debug_printf("[TP] resume from pause"NL);
    
    pthread_mutex_unlock(&pool->lock);
    return 0;
}

int threadpool_submit(struct threadpool *pool, void *(*func)(void*), void *arg){
    struct jobinfo *tmp_job;

    tmp_job = calloc(1, sizeof(struct jobinfo));
    tmp_job->func = func;
    tmp_job->arg  = arg;
    
    pthread_mutex_lock(&pool->lock);
    
    list_push(&pool->jobq, &tmp_job->node);
    pool->pending_jobs++;

    // debug_printf("submit job"NL);

    if (!pool->is_paused && pool->n_idle != 0){
        /* Threadpool is running and have idling workers. */
        if ((pool->n_idle == pool->n_workers) && (pool->pending_jobs != 0)){
            /* If 1)all worker are idling and 2)there is pending job.
                If all workers are idling, the gate must have been
                unlocked, lock the gate, so waiting routines work.
            */
            /* BUGFIX
                Erroneously not locking the gate while there is still
                job.
            */
            /*
                Only several functions can change the state of "gate":
                1) `submit` and `resume` lock the gate.
                2) (the last) worker thread unlocks the gate.
                Other APIs do not change the state of gate.
            */
            gate_lock(pool->wgate);
        }
        pthread_cond_signal(&pool->cond);
    }

    pthread_mutex_unlock(&pool->lock);
    return 0;
}

void threadpool_free(struct threadpool *pool){
    struct list       *tmp_node = NULL;
    struct thread_ctx *tmp_tctx = NULL;
    struct jobinfo    *tmp_job  = NULL;

    pthread_mutex_lock(&pool->lock);
    if (!list_is_empty(&pool->threadq)){
        list_traverse(&pool->threadq, tmp_node){
            tmp_tctx = list_entry(tmp_node, struct thread_ctx, node);
            tmp_tctx->is_terminated = 1;
        }
        pthread_cond_broadcast(&pool->cond);
    }
    pthread_mutex_unlock(&pool->lock);


    if (!list_is_empty(&pool->threadq)){
        list_traverse(&pool->threadq, tmp_node){
            tmp_tctx = list_entry(tmp_node, struct thread_ctx, node);
            debug_printf("joining %lX"NL, tmp_tctx->tid);
            pthread_join(tmp_tctx->tid, NULL);
        }
    }

    for (;!list_is_empty(&pool->threadq);){
        tmp_node = list_pop(&pool->threadq);
        tmp_tctx = list_entry(tmp_node, struct thread_ctx, node);
        free(tmp_tctx);
    }
    tmp_tctx = NULL;

    for (;!list_is_empty(&pool->jobq);){
        tmp_node = list_pop(&pool->jobq);
        tmp_job = list_entry(tmp_node, struct jobinfo, node);
        free(tmp_job);
    }
    tmp_job = NULL;

    gate_free(pool->wgate);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->cond);

    free(pool);

    return;
}

int threadpool_scale_to(struct threadpool *pool, unsigned int n){
    if (n == pool->n_workers){
        return n;
    } else if (n >= MAX_WORKERS){
        /* Touched hard limit. */
        return -1;
    }
    if (n < pool->n_workers){
        downscale(pool, pool->n_workers - n);
    } else {
        upscale(pool, n - pool->n_workers);
    }
    return pool->n_workers;
}