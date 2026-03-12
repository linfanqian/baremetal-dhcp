#include "cqueue.h"

int cq_empty(cq_t *cq) { return cq->head == cq->tail; }
int cq_full(cq_t *cq) { return (cq->head+1) % (CQ_N) == cq->tail; }

unsigned cq_nelem(cq_t *cq) { return (cq->head - cq->tail + CQ_N) % (CQ_N); }
unsigned cq_nspace(cq_t *cq) { return (CQ_N-1) - cq_nelem(cq); }

int cq_pop(cq_t *cq, cqe_t *cqe) {
    if(cq_empty(cq)) return CQ_OP_FAIL;

    unsigned tail = cq->tail;
    // must occur in order.
    *cqe = cq->c_buf[tail];     // 1
    cq->tail = (tail+1)% (CQ_N);  // 2
    return CQ_OP_SUCCESS;
}

int cq_push(cq_t *cq, cqe_t cqe) {
    if(cq_full(cq)) return CQ_OP_FAIL;

    unsigned head = cq->head;

    // must occur in order
    cq->c_buf[head] = cqe;  // 1
    cq->head = (head + 1) % (CQ_N); // 2
    return CQ_OP_SUCCESS;
}

int cq_peek(cq_t *cq, cqe_t *cqe) {
    if(cq_empty(cq)) return CQ_OP_FAIL;

    *cqe = cq->c_buf[cq->tail];
    return CQ_OP_SUCCESS;
}

int cq_pop_n(cq_t *cq, void *data, unsigned n) {
    if(cq_nelem(cq) < n) return CQ_OP_FAIL;
    
    cqe_t *cqes = (cqe_t *)data;
    
    // ASSUME SINGLE THREAD AND NO ERROR
    for (unsigned i = 0; i < n; i++)
        cq_pop(cq, cqes + i);

    return CQ_OP_SUCCESS;
}

int cq_peek_n(cq_t *cq, cqe_t *cqes, unsigned n) {
    int sz = cq_nelem(cq);
    if(n > sz) return CQ_OP_FAIL;

    unsigned tail = cq->tail;
    int rc = cq_pop_n(cq, (void *)cqes, n);

    cq->tail = tail;
    return rc;
}

int cq_push_n(cq_t *cq, const void *data, unsigned n) {
    if (n <= 0 || cq_nspace(cq) < n) return CQ_OP_FAIL;

    const cqe_t *cqes = (const cqe_t *)data;

    // ASSUME SINGLE THREAD AND NO ERROR
    for(int i = 0; i < n; i++)
        cq_push(cq, cqes[i]);

    return CQ_OP_SUCCESS;
}

int cq_push32(cq_t *cq, int x) {
    return cq_push_n(cq, &x, sizeof x);
}

int cq_pop32(cq_t *cq, int *x) {
    return cq_pop_n(cq, x, sizeof *x);
}

int cq_push64(cq_t *cq, uint64_t x) {
    return cq_push_n(cq, &x, sizeof x);
}

int cq_pop64(cq_t *cq, uint64_t *x) {
    return cq_pop_n(cq, x, sizeof *x);
}

int cq_ok(cq_t *cq) {
    if(cq->fence != 0x12345678) return CQ_OP_FAIL;
    return CQ_OP_SUCCESS;
}

int cq_init(cq_t *cq) {
    // TODO: INITIALIZE EVERYTHING TO ZERO
    for (unsigned i = 0; i < CQ_N; i++)
        cq->c_buf[i] = 0;

    cq->fence = 0x12345678;
    cq->head = cq->tail = 0;
    
    // TODO: BE UNHAPPY
    if (!cq_empty(cq) || cq_full(cq) || cq_nelem(cq) != 0) 
        return CQ_OP_FAIL;

    cqe_t e = 0x12;
    if (cq_pop(cq,&e) != CQ_OP_FAIL || e != 0x12) 
        return CQ_OP_FAIL;

    return CQ_OP_SUCCESS;
}
