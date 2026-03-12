#include <pthread.h>

#include "lib/cqueue.h"
#include "client-queue.h"

static pthread_mutex_t client_queue_lock = PTHREAD_MUTEX_INITIALIZER;
static cq_t cq;

void client_queue_init() {
    pthread_mutex_lock(&client_queue_lock);
    cq_init(&cq);
    pthread_mutex_unlock(&client_queue_lock);
}

struct client_async * pop_client() {
    uint64_t c_async_ptr;
    int rc;
    pthread_mutex_lock(&client_queue_lock);
    rc = cq_pop64(&cq, &c_async_ptr);
    pthread_mutex_unlock(&client_queue_lock);

    if (rc != CQ_OP_SUCCESS)
        return NULL;

    return (struct client_async *) c_async_ptr;
}

void push_client(struct client_async *c_async) {
    uint64_t c_async_ptr = (uint64_t) c_async;
    pthread_mutex_lock(&client_queue_lock);
    cq_push64(&cq, c_async_ptr);
    pthread_mutex_unlock(&client_queue_lock);
}
