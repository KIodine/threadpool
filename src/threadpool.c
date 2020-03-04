#include "threadpool.h"

#define NL "\n"

static void *worker(void* arg);
//static void  force_dismiss(struct threadpool *pool);
static void  upscale(struct threadpool *pool, unsigned int n);
static void  downscale(struct threadpool *pool, unsigned int n);

static
void *worker(void *arg){
    struct thread_ctx *tctx;
    struct threadpool *pool;
    pthread_mutex_t *lock;
    pthread_cond_t  *cond;
    struct jobinfo *job  = NULL;
    struct list *tmp_node = NULL;
    struct list *jobhead = NULL;
    int was_working  = 0;
    uint64_t const efd_buf = 1;
    
    assert(arg != NULL);
    
    // avoid double pointer access
    tctx    = (struct thread_ctx*)arg;
    pool    = tctx->pool;
    lock    = &pool->lock;
    cond    = &pool->cond;
    jobhead = &pool->jobq;

    printf("[%lX] is ready"NL, tctx->tid);
    for (;;){
        pthread_mutex_lock(lock);
        printf("[%lX] main lock acquired"NL, tctx->tid);
        if (was_working){
            pool->n_idle++;
            was_working = 0;
        }
        for (;;){
            if (tctx->is_terminated){
                // if we're not "pop"ed, there is at least one node we
                // connect to -- the head node
                if (list_is_empty(&tctx->node)){
                    // we're poped from `threadq`, now the thread
                    // handles itself
                    printf("[\x1b[1;31m%lX\x1b[0m] being downscaled"NL, tctx->tid);
                    free(tctx);
                } else {
                    printf("[%lX] terminated"NL, tctx->tid);
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
                printf("[%lX] get to work"NL, tctx->tid);
                pthread_mutex_unlock(lock);
                break;
            } else {
                if (pool->n_idle == pool->n_workers){
                    // the last worker here would write the
                    // `no_working` eventfd
                    // ^(the new worker join during pausing/no job would
                    //   write as well)
                    printf("[%lX] last worker gate open"NL, tctx->tid);
                    write(pool->no_working, &efd_buf, sizeof(uint64_t));
                }
            }
            printf("[%lX] no job to do"NL, tctx->tid);
            pthread_cond_wait(cond, lock);
        }
        job->func(job->arg);
        free(job);
    }
end:
    pthread_exit(NULL); // never return to the caller
}


static
void upscale(struct threadpool *pool, unsigned int n){
    struct thread_ctx *tmp;
    // TODO...
    /*
    acquire lock
    alloc `struct thread_ctx`
    put tctx into `threadq`
    launch thread
    n_workers += n
    n_idle += n
    release lock
    */
    printf("[TP] upscale by %d"NL, n);
    
    pthread_mutex_lock(&pool->lock);
    for (unsigned int i = 0 ; i < n; ++i){
        tmp = calloc(1, sizeof(struct thread_ctx));
        tmp->pool = pool;
        list_push(&pool->threadq, &tmp->node);
        pthread_create(&tmp->tid, NULL, worker, tmp);
    }
    pool->n_workers += n;
    pool->n_idle    += n;
    pthread_mutex_unlock(&pool->lock);
    return;
}

static
void downscale(struct threadpool *pool, unsigned int n){
    struct thread_ctx *tmp_ctx;
    struct list *tmp_node;
    // TODO...
    /*
    acquire lock
    for n to dismiss
        pop `struct thread_ctx`
        set `is_terminate` flag
        detach these threads
    n_workers -= n
    n_idle -= n
    if not pausing:
        wake all
    release lock
    ...
    */

    printf("[TP] downscale by %d"NL, n);

    pthread_mutex_lock(&pool->lock);
    for (unsigned int i = 0; i < n; ++i){
        tmp_node = list_get(&pool->threadq);
        tmp_ctx = list_entry(tmp_node, struct thread_ctx, node);
        tmp_ctx->is_terminated = 1;
        pthread_detach(tmp_ctx->tid);
    }
    pool->n_workers -= n;
    pool->n_idle    -= n;
    if (!pool->is_paused){
        pthread_cond_broadcast(&pool->cond);
    }
    pthread_mutex_unlock(&pool->lock);
    return;
}


struct threadpool *threadpool_alloc(void){
    struct threadpool *pool;
    int ret;
    const uint64_t efd_buf = 1;
    
    pool = calloc(1, sizeof(struct threadpool));
    list_init(pool->threadq);
    list_init(pool->jobq);

    pool->no_working = eventfd(0, 0);
    write(pool->no_working, &efd_buf, sizeof(uint64_t));
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);

    return pool;
}

void threadpool_wait(struct threadpool *pool){
    uint64_t efd_buf = 0;
    /*
    acquire lock
    if pausing:
        release lock
        return
    release lock
    read on the `no_working` eventfd
    ^(this would block until jobs are consumed "natually")
    write `no_working` fd
    ^(put back the flag)
    */
    pthread_mutex_lock(&pool->lock);
    if (pool->is_paused){
        pthread_mutex_unlock(&pool->lock);
        return;
    }
    pthread_mutex_unlock(&pool->lock);

    printf("[TP] waiting for jobs complete"NL);

    read(pool->no_working, &efd_buf, sizeof(uint64_t));
    efd_buf = 1;
    write(pool->no_working, &efd_buf, sizeof(uint64_t));
    // put is back in case other API depends on this deadlocks.

    assert(pool->pending_jobs == 0);

    return;
}

int threadpool_pause(struct threadpool *pool){
    uint64_t efd_buf = 0;
    /*
    acquire lock
    if n_workers == 0 or is pausing
        release lock
        return
    set pause flag
    release lock
    wait on `no_working` eventfd
    ^(could from pause flag or just no job to do)
    write `no_working` fd
    ^(put it back)
    */
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

    printf("[TP] start pause"NL);

    // put is back in case other API depends on this deadlocks.
    return 0;
}
// a `force_pause` method (using a signal slot)?

int threadpool_resume(struct threadpool *pool){
    uint64_t efd_buf = 0;
    /*
    acquire lock
    if no thread launched or not pausing
        release lock
        return
    unset pause flag
    clear `no_working` eventfd
    (could be set anyway if still no work, this is for preventing
     any other call depends on the blocking read on eventfd)
    signal all
    release lock
    */
    pthread_mutex_lock(&pool->lock);
    if (pool->n_workers == 0 || !pool->is_paused){
        pthread_mutex_unlock(&pool->lock);
    }
    pool->is_paused = 0;
    read(pool->no_working, &efd_buf, sizeof(uint64_t));
    // does it guarenteed during pause, `no_working` always readable?
    pthread_cond_broadcast(&pool->cond);
    
    printf("[TP] resume from pause"NL);
    
    pthread_mutex_unlock(&pool->lock);
    return 0;
}

int threadpool_submit(struct threadpool *pool, void *(*func)(void*), void *arg){
    struct jobinfo *tmp_job;
    uint64_t efd_buf = 0;
    /*
    acquire lock
    put work in queue
    pool->pending_jobs++;
    if not pausing and n_idle != 0:
        if n_idle == n_workers:
            clear `no_working` flag?
            ^(could from real no work)
            ^(does it guarenteed no_working is set?)
            -> yes, for now
        signal one
    release lock
    */
    pthread_mutex_lock(&pool->lock);
    
    tmp_job = calloc(1, sizeof(struct jobinfo));
    tmp_job->func = func;
    tmp_job->arg = arg;
    list_push(&pool->jobq, &tmp_job->node);
    pool->pending_jobs++;

    printf("[TP] submit job"NL);

    /*
        FIXME:
        1)  multiple `submit` before threads get into work area
            caused `submit` deadlocks on the `read`.
        2)  some submitted jobs are lost? maybe error in list
            `list_is_empty`?
    */

    if (!pool->is_paused && pool->n_idle != 0){
        if ((pool->n_idle == pool->n_workers) && (pool->pending_jobs == 0)){
            // every worker is waiting
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
    uint64_t efd_buf = 0;
    /*
    acquire lock
    if (!list_is_empty(&pool->threadq)){
        traverse all thread and set the terminate flag
        wake all of them
    }
    release lock
    read the eventfd of `no_working`
    ^(this would block until `no_working` becomes non-zero)
     (also, reading from a eventfd zeros it if proper flag is set)
     (what if free after wait? the `no_working` flag would already
      consumed by `threadpool_wait`)
      -> just not wait
      -> now pause/wait function will put this flag back once read
    join on all threads
    */
    pthread_mutex_lock(&pool->lock);
    if (!list_is_empty(&pool->threadq)){
        list_traverse(&pool->threadq, tmp_node){
            tmp_tctx = list_entry(tmp_node, struct thread_ctx, node);
            tmp_tctx->is_terminated = 1;
        }
        pthread_cond_broadcast(&pool->cond);
    }
    pthread_mutex_unlock(&pool->lock);

    // don't read on `no_working` - if they're all running and terminate
    // flag sets, no thread is going to write(or say set) the `no_working`
    // flag.

    if (!list_is_empty(&pool->threadq)){
        list_traverse(&pool->threadq, tmp_node){
            tmp_tctx = list_entry(tmp_node, struct thread_ctx, node);
            printf("[TP] joining %lX"NL, tmp_tctx->tid);
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
    /*
    if n == n_workers:
        return
    if n < n_workers:
        downscale(n_workers - n)
    else:
        upscale(n - n_workers)
    */
    if (n == pool->n_workers || n >= MAX_WORKERS){
        return 0;
    }
    if (n < pool->n_workers){
        downscale(pool, pool->n_workers - n);
    } else {
        upscale(pool, n - pool->n_workers);
    }
    return 0;
}