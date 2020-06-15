#ifndef GATELOCK_H
#define GATELOCK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>


struct gate {
    int is_locked;
    pthread_mutex_t gmutx;
    pthread_cond_t  gcond;
};


struct gate *gate_alloc(void);
void gate_free(struct gate *glock);

int gate_lock(struct gate *glock);
int gate_unlock(struct gate *glock);
int gate_wait(struct gate *glock);


#endif /* GATELOCK_H */