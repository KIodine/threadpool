# Scalable Threadpool in C

A primitive threadpool implemente in C language.

Note this code is currently Linux-specific (because of the use of `eventfd`).

# Table of Content
- [Feature](#Feature)
- [Build](#Build)
- [Example](#Use)
- [License](#License)

# Feature
- Dynamically scalable (by using linked list)

# Build
```=
make static
# or
make shared
```

# Example
```c=
void *routine(void *arg){
    /*
        your working routine, worker threads won't catch the result,
        if you need the result, you must create some sort of pipe by yourself.
    */
}

/* ... */

struct threadpool *tp = NULL;

tp = threadpool_alloc();
/* threadpool has 0 worker initially, you must "hire" them */
threadpool_scale_to(tp, /* number of workers */);

/* submit jobs, this is thread-safe */
threadpool_submit(tp, routine, /* arg */);

/* ... */

/* block until all current jobs are done */
threadpool_pause(tp);

/*
    during pausing, you can still put jobs into queue, but no worker
    will pick the job.
*/

/* put all workers work again */
threadpool_resume(tp);

/* ... */

/* shrink the scale of workers, detaches some thread and return soon */
threadpool_scale_to(tp, /* nworkers lower than previous set */);

/*
    block until all jobs consumed, this would not cause the pool unusable.
*/
threadpool_wait(tp);


/* waits until all jobs complete, then destroy all pending jobs */
threadpool_free(tp);
tp = NULL;

```

# License
[![License](http://img.shields.io/:license-mit-blue.svg?style=flat-square)](http://badges.mit-license.org)

Threadpool is distributed under MIT license.
