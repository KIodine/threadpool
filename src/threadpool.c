#include "threadpool.h"

#define NL "\n"


/* follow `assert` method */
#ifndef NDEBUG
    #define MSG_PREFIX "[ThreadPool] "
    #define debug_printf(fmt, ...) printf(MSG_PREFIX fmt, ##__VA_ARGS__)
#else
    #define MSG_PREFIX
    #define debug_printf(ignore, ...) ((void)0)
#endif


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
    uint64_t const efd_buf = 1;
    
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
                /*
                    if we're not "pop"ed, there is at least one node we
                    connect to -- the head node
                */
                if (list_is_empty(&tctx->node)){
                    /*
                        we're poped from `threadq`, now the thread
                        handles itself
                    */
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
                if (pool->n_idle == pool->n_workers){
                    /*
                        the last worker here would write the
                        `no_working` eventfd
                        ^(the new worker join during pausing/no job would
                        write as well)
                    */
                    debug_printf("[%lX] last worker gate open"NL, tctx->tid);
                    write(pool->no_working, &efd_buf, sizeof(uint64_t));
                }
            }
            debug_printf("[%lX] no job to do"NL, tctx->tid);
            pthread_cond_wait(cond, lock);
        }
        job->func(job->arg);
        free(job);
    }
end:
    pthread_exit(NULL); /* never return to the caller */
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
    int ret, tmpfd;
    const uint64_t efd_buf = 1;
    
    /* always returns a "valid" pointer on Linux, until killed by oom */
    pool = calloc(1, sizeof(struct threadpool));
    list_init(pool->threadq);
    list_init(pool->jobq);

    tmpfd = eventfd(0, 0);
    if (tmpfd == -1){
        perror("create eventfd");
        goto err_evfd;
    }
    pool->no_working = tmpfd;
    
    write(pool->no_working, &efd_buf, sizeof(uint64_t));
    
    ret = pthread_mutex_init(&pool->lock, NULL);
    if (ret != 0){
        errno = ret;
        perror("create mutex");
        goto err_lock;
    }

    ret = pthread_cond_init(&pool->cond, NULL);
    if (ret != 0){
        errno = ret;
        perror("create cond");
        goto err_cond;
    }

    return pool;
err_cond:
    pthread_mutex_destroy(&pool->lock);
err_lock:
    close(tmpfd);
err_evfd:
    free(pool);
    return NULL;
}

void threadpool_wait(struct threadpool *pool){
    uint64_t efd_buf = 0;

    pthread_mutex_lock(&pool->lock);
    if (pool->is_paused){
        pthread_mutex_unlock(&pool->lock);
        return;
    }
    pthread_mutex_unlock(&pool->lock);

    debug_printf("waiting for jobs complete"NL);

    read(pool->no_working, &efd_buf, sizeof(uint64_t));
    efd_buf = 1;
    write(pool->no_working, &efd_buf, sizeof(uint64_t));
    /* put is back in case other API depends on this deadlocks. */

    assert(pool->pending_jobs == 0);

    return;
}

int threadpool_pause(struct threadpool *pool){
    uint64_t efd_buf = 0;

    pthread_mutex_lock(&pool->lock);
    if (pool->n_workers == 0 || pool->is_paused){
        pthread_mutex_unlock(&pool->lock);
        return 0;
    }
    pool->is_paused = 1;
    pthread_mutex_unlock(&pool->lock);

    read(pool->no_working, &efd_buf, sizeof(uint64_t));
    efd_buf = 1;
    write(pool->no_working, &efd_buf, sizeof(uint64_t));

    debug_printf("start pause"NL);

    /* put is back in case other API depends on this deadlocks. */
    return 0;
}

int threadpool_resume(struct threadpool *pool){
    uint64_t efd_buf = 0;

    pthread_mutex_lock(&pool->lock);
    if (pool->n_workers == 0 || !pool->is_paused){
        pthread_mutex_unlock(&pool->lock);
    }
    pool->is_paused = 0;
    read(pool->no_working, &efd_buf, sizeof(uint64_t));
    /* does it guarenteed during pause, `no_working` always readable? */
    pthread_cond_broadcast(&pool->cond);
    
    debug_printf("[TP] resume from pause"NL);
    
    pthread_mutex_unlock(&pool->lock);
    return 0;
}

int threadpool_submit(struct threadpool *pool, void *(*func)(void*), void *arg){
    struct jobinfo *tmp_job;
    uint64_t efd_buf = 0;

    pthread_mutex_lock(&pool->lock);
    
    tmp_job = calloc(1, sizeof(struct jobinfo));
    tmp_job->func = func;
    tmp_job->arg = arg;
    list_push(&pool->jobq, &tmp_job->node);
    pool->pending_jobs++;

    debug_printf("submit job"NL);

    if (!pool->is_paused && pool->n_idle != 0){
        if ((pool->n_idle == pool->n_workers) && (pool->pending_jobs == 0)){
            /* every worker is waiting */
            read(pool->no_working, &efd_buf, sizeof(uint64_t));
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

    /*
        don't read on `no_working` - if they're all running and terminate
        flag sets, no thread is going to write(or say set) the `no_working`
        flag.
    */

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

    close(pool->no_working);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->cond);

    free(pool);

    return;
}

int threadpool_scale_to(struct threadpool *pool, unsigned int n){
    if (n == pool->n_workers || n >= MAX_WORKERS){
        /* no size change or touched hard limit */
        return -1;
    }
    if (n < pool->n_workers){
        downscale(pool, pool->n_workers - n);
    } else {
        upscale(pool, n - pool->n_workers);
    }
    return pool->n_workers;
}