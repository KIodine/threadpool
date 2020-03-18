#include "list.h"


void list_push(struct list *head, struct list *node){
    struct list *hnext = head->next;
    node->next  = hnext;
    node->prev  = head;
    hnext->prev = node;
    head->next  = node;
    return;
}

struct list *list_pop(struct list *head){
    struct list *tmp, *tnext;
    if (list_is_empty(head)) return NULL;
    tmp   = head->next;
    tnext = tmp->next;
    head->next = tnext;
    tnext->prev = head;
    tmp->next = tmp;
    tmp->prev = tmp;
    return tmp;
}

void list_append(struct list *head, struct list *node){
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
    return;
}

struct list *list_get(struct list *head){
    struct list *tmp, *tprev;
    if (list_is_empty(head)) return NULL;
    tmp     = head->prev;
    tprev   = tmp->prev;
    head->prev  = tprev;
    tprev->next = head;
    tmp->next = tmp;
    tmp->prev = tmp;
    return tmp;
}

void list_delete(struct list *node){
    struct list *nprev, *nnext;
    nprev = node->prev;
    nnext = node->next;
    nprev->next = nnext;
    nnext->prev = nnext;
    return;
}
