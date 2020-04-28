#include "gate.h"


struct gate *gate_alloc(void){
    struct gate *gl = NULL;
    int ret;

    gl = calloc(1, sizeof(struct gate));
    
    ret = pthread_mutex_init(&gl->gmutx, NULL);
    if (ret != 0){
        perror("Gate lock init mutex");
        goto err_init_mutex;
    }

    ret = pthread_cond_init(&gl->gcond, NULL);
    if (ret != 0){
        perror("Gate lock init cond");
        goto err_init_cond;
    }

    return gl;
    /* Error handling. */
err_init_cond:
    pthread_mutex_destroy(&gl->gmutx);
err_init_mutex:
    free(gl);
    return NULL;
}

void gate_free(struct gate *glock){
    int ret;
    
    if (glock == NULL){goto end;}
    
    /* Q: Die if destroy fail? */
    ret = pthread_mutex_destroy(&glock->gmutx);
    if (ret != 0){
        perror("destroy gate lock mutex");
        goto end;
    }
    
    ret = pthread_cond_destroy(&glock->gcond);
    if (ret != 0){
        perror("destroy gate lock cond");
        goto end;
    }

    free(glock);
end:
    return;
}

int gate_lock(struct gate *glock){
    pthread_mutex_lock(&glock->gmutx);
    glock->is_locked = 1;
    pthread_mutex_unlock(&glock->gmutx);
    return 0;
}

int gate_unlock(struct gate *glock){
    pthread_mutex_lock(&glock->gmutx);
    glock->is_locked = 0;
    pthread_cond_broadcast(&glock->gcond);
    pthread_mutex_unlock(&glock->gmutx);
    return 0;
}

int gate_wait(struct gate *glock){
    pthread_mutex_lock(&glock->gmutx);
    if (glock->is_locked == 1){
        pthread_cond_wait(&glock->gcond, &glock->gmutx);
    }
    pthread_mutex_unlock(&glock->gmutx);
    return 0;
}
