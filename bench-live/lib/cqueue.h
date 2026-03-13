/*
 * this is borrowed from CS140E libpi/cq.c implementation
 * with minor adaptations/reductions for most simple interface
 */

// use memory barriers.  volatile is really bad.
#ifndef __CQUEUE_H__
#define __CQUEUE_H__

// simple lock-free circular queue.

#include <stdint.h>


// simple circular FIFO Q.   
//
// if we want to store records?
// make the code templatized on this? (ala fraser and hanson)  
typedef unsigned char cqe_t;

#ifndef CQ_N
#   define CQ_N (4096 * 8 * sizeof(void *))    // may be defined elsewhere by the client?
#endif

typedef struct {
    // single mutator of head, single mutator of tail = lock free.  
    cqe_t c_buf[CQ_N];
    unsigned fence;
    unsigned head, tail;

    // number of times a push failed b/c there were more than N elements.
    // feel like not needed for now?
    // unsigned overflow;
} cq_t;

// operation-related status
enum {
    CQ_OP_FAIL = 0,
    CQ_OP_SUCCESS = 1,
};


int cq_empty(cq_t *q);
int cq_full(cq_t *q);

unsigned cq_nelem(cq_t *q);
unsigned cq_nspace(cq_t *q);

int cq_pop(cq_t *c, cqe_t *e);
int cq_push(cq_t *c, cqe_t x);
int cq_peek(cq_t *c, cqe_t *e);

int cq_pop_n(cq_t *c, void *data, unsigned n);
int cq_peek_n(cq_t *c, cqe_t *v, unsigned n);
int cq_push_n(cq_t *c, const void *data, unsigned n);

// macro used for pushing and poping arbitrary sizes
#define cq_push_type(cq, x) cq_push_n(cq, &(x), sizeof(x))
#define cq_pop_type(cq, x)  cq_pop_n(cq, &(x), sizeof(x))

int cq_push32(cq_t *c, int x); 
int cq_pop32(cq_t *c, int *x);
int cq_push64(cq_t *c, uint64_t x); 
int cq_pop64(cq_t *c, uint64_t *x);

int cq_ok(cq_t *c); 
int cq_init(cq_t *c);

#endif
