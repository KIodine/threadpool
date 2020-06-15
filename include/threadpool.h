#ifndef THREADPOOL_H
#define THREADPOOL_H

#define MAX_WORKERS 128UL

/*
    - cleanup when thread terminates accidentally?
      -> just let them die, hiding no bug
*/

/* declare. */
struct threadpool;


struct threadpool *threadpool_alloc(void);
void threadpool_free(struct threadpool *pool);

void threadpool_wait(struct threadpool *pool);
int threadpool_submit(struct threadpool *pool, void *(*func)(void*), void *arg);

int threadpool_scale_to(struct threadpool *pool, unsigned int n);
int threadpool_pause(struct threadpool *pool);
int threadpool_resume(struct threadpool *pool);


#endif /* THREADPOOL_H */