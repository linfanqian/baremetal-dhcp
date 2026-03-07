#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "cqueue.h"

static int test_run = 0, test_passed = 0;

#define CHECK(cond, msg) do {               \ 
    test_run++;                             \
    if (cond) { test_passed++;  }           \
    else  {                                 \
        printf("  FAIL[%s:%d]: %s\n",       \
                __FILE__, __LINE__, msg);   \
    }                                       \
} while (0);

static cq_t *make_queue(void) {
    cq_t *cq = calloc(1, sizeof(cq_t));
    assert(cq);
    int rc = cq_init(cq);
    assert(rc == CQ_OP_SUCCESS);
    return cq;
}


// ---------------------------------------------------------------------------
// 1. init sanity
// ---------------------------------------------------------------------------
static void test_init(void) {
    printf("test_init\n");
    cq_t *cq = make_queue();
    CHECK(cq_ok(cq)    == CQ_OP_SUCCESS, "fence ok after init");
    CHECK(cq_empty(cq) == 1,             "empty after init");
    CHECK(cq_full(cq)  == 0,             "not full after init");
    CHECK(cq_nelem(cq) == 0,             "nelem == 0 after init");
    CHECK(cq_nspace(cq) == CQ_N - 1,    "nspace == CQ_N-1 after init");
    free(cq);
}

// ---------------------------------------------------------------------------
// 2. push / pop single elements
// ---------------------------------------------------------------------------
static void test_push_pop_single(void) {
    printf("test_push_pop_single\n");
    cq_t *cq = make_queue();

    cqe_t val = 0xAB;
    CHECK(cq_push(cq, val) == CQ_OP_SUCCESS, "push succeeds");
    CHECK(cq_nelem(cq) == 1,                 "nelem == 1 after push");
    CHECK(cq_empty(cq) == 0,                 "not empty after push");

    cqe_t out = 0;
    CHECK(cq_pop(cq, &out) == CQ_OP_SUCCESS, "pop succeeds");
    CHECK(out == val,                         "pop returns correct value");
    CHECK(cq_empty(cq) == 1,                 "empty after pop");

    free(cq);
}

// ---------------------------------------------------------------------------
// 3. FIFO ordering
// ---------------------------------------------------------------------------
static void test_fifo_order(void) {
    printf("test_fifo_order\n");
    cq_t *cq = make_queue();
    unsigned n = CQ_N - 1; // fill to capacity

    for (unsigned i = 0; i < n; i++)
        cq_push(cq, (cqe_t)(i & 0xFF));

    CHECK(cq_full(cq) == 1, "full after pushing CQ_N-1 elements");

    int order_ok = 1;
    for (unsigned i = 0; i < n; i++) {
        cqe_t out;
        cq_pop(cq, &out);
        if (out != (cqe_t)(i & 0xFF)) { order_ok = 0; break; }
    }
    CHECK(order_ok,          "FIFO order preserved");
    CHECK(cq_empty(cq) == 1, "empty after draining");

    free(cq);
}

// ---------------------------------------------------------------------------
// 4. push on full / pop on empty return FAIL
// ---------------------------------------------------------------------------
static void test_boundary_failures(void) {
    printf("test_boundary_failures\n");
    cq_t *cq = make_queue();

    // pop empty
    cqe_t e = 0x55;
    CHECK(cq_pop(cq, &e) == CQ_OP_FAIL, "pop on empty fails");
    CHECK(e == 0x55,                     "pop on empty does not clobber out");

    // fill it
    while (!cq_full(cq)) cq_push(cq, 0xAA);

    CHECK(cq_push(cq, 0xBB) == CQ_OP_FAIL, "push on full fails");
    CHECK(cq_nelem(cq) == CQ_N - 1,         "nelem unchanged after failed push");

    free(cq);
}

// ---------------------------------------------------------------------------
// 5. peek does not consume
// ---------------------------------------------------------------------------
static void test_peek(void) {
    printf("test_peek\n");
    cq_t *cq = make_queue();

    cq_push(cq, 0x11);
    cq_push(cq, 0x22);

    cqe_t p = 0;
    CHECK(cq_peek(cq, &p) == CQ_OP_SUCCESS, "peek succeeds");
    CHECK(p == 0x11,                         "peek returns head value");
    CHECK(cq_nelem(cq) == 2,                 "nelem unchanged after peek");

    cqe_t out;
    cq_pop(cq, &out);
    CHECK(out == 0x11, "pop after peek returns same value");

    free(cq);
}

// ---------------------------------------------------------------------------
// 6. push_n / pop_n
// ---------------------------------------------------------------------------
static void test_push_pop_n(void) {
    printf("test_push_pop_n\n");
    cq_t *cq = make_queue();

    cqe_t src[8] = {1,2,3,4,5,6,7,8};
    unsigned n = 8;
    if (n > (unsigned)(CQ_N - 1)) n = CQ_N - 1;

    CHECK(cq_push_n(cq, src, n) == CQ_OP_SUCCESS, "push_n succeeds");
    CHECK(cq_nelem(cq) == n,                       "nelem == n after push_n");

    cqe_t dst[8] = {0};
    CHECK(cq_pop_n(cq, dst, n) == CQ_OP_SUCCESS, "pop_n succeeds");
    CHECK(memcmp(src, dst, n * sizeof(cqe_t)) == 0, "pop_n data matches push_n");
    CHECK(cq_empty(cq) == 1,                        "empty after pop_n");

    // fail cases
    CHECK(cq_push_n(cq, src, 0) == CQ_OP_FAIL,      "push_n(0) fails");
    CHECK(cq_pop_n(cq, dst, 1)  == CQ_OP_FAIL,       "pop_n on empty fails");

    free(cq);
}

// ---------------------------------------------------------------------------
// 7. peek_n does not consume
// ---------------------------------------------------------------------------
static void test_peek_n(void) {
    printf("test_peek_n\n");
    cq_t *cq = make_queue();

    cqe_t src[4] = {10, 20, 30, 40};
    cq_push_n(cq, src, 4);

    cqe_t peeked[4] = {0};
    CHECK(cq_peek_n(cq, peeked, 4) == CQ_OP_SUCCESS,         "peek_n succeeds");
    CHECK(memcmp(src, peeked, 4 * sizeof(cqe_t)) == 0,        "peek_n data correct");
    CHECK(cq_nelem(cq) == 4,                                   "nelem unchanged after peek_n");

    // peek more than available
    cqe_t tmp[8];
    CHECK(cq_peek_n(cq, tmp, 8) == CQ_OP_FAIL, "peek_n past size fails");

    free(cq);
}

// ---------------------------------------------------------------------------
// 8. wraparound — push/pop past the end of the buffer
// ---------------------------------------------------------------------------
static void test_wraparound(void) {
    printf("test_wraparound\n");
    cq_t *cq = make_queue();

    // advance head/tail halfway through the buffer
    unsigned half = (CQ_N - 1) / 2;
    for (unsigned i = 0; i < half; i++) { cq_push(cq, (cqe_t)i); }
    for (unsigned i = 0; i < half; i++) { cqe_t e; cq_pop(cq, &e); }

    // now push/pop across the wraparound boundary
    unsigned n = CQ_N - 1;
    int ok = 1;
    for (unsigned i = 0; i < n; i++) {
        if (cq_push(cq, (cqe_t)(i & 0xFF)) != CQ_OP_SUCCESS) { ok = 0; break; }
    }
    CHECK(ok, "push across wraparound succeeds");

    ok = 1;
    for (unsigned i = 0; i < n; i++) {
        cqe_t e;
        cq_pop(cq, &e);
        if (e != (cqe_t)(i & 0xFF)) { ok = 0; break; }
    }
    CHECK(ok,            "pop across wraparound correct");
    CHECK(cq_empty(cq),  "empty after wraparound drain");

    free(cq);
}

// ---------------------------------------------------------------------------
// 9. push32 / pop32 (packs sizeof(int) cqe_t elements)
// ---------------------------------------------------------------------------
static void test_push_pop32(void) {
    printf("test_push_pop32\n");
    cq_t *cq = make_queue();

    int x = 0xDEADBEEF;
    CHECK(cq_push32(cq, x) == CQ_OP_SUCCESS,  "push32 succeeds");
    CHECK(cq_nelem(cq) == sizeof(int),         "nelem == sizeof(int) after push32");

    int y = 0;
    CHECK(cq_pop32(cq, &y) == CQ_OP_SUCCESS,  "pop32 succeeds");
    CHECK(y == x,                              "pop32 roundtrips value");
    CHECK(cq_empty(cq),                        "empty after pop32");

    free(cq);
}
// ---------------------------------------------------------------------------
// 9. push_type / pop_type
// ---------------------------------------------------------------------------
static void test_push_pop_type(void) {
    printf("test_push_pop_type\n");
    cq_t *cq = make_queue();
    

    unsigned long long x = 0xDEADBEEFDEADBEEF;

    CHECK(cq_push_type(cq, x) == CQ_OP_SUCCESS,    "push arbitrary type succeeds");
    CHECK(cq_nelem(cq) == sizeof(x),               "nelem == sizeof(unsigned long long) after push_type");
    
    unsigned long long y = 0;
    CHECK(cq_pop_type(cq, y) == CQ_OP_SUCCESS,      "pop type succeeds");
    CHECK(y == x,                              "pop type roundtrips value");
    CHECK(cq_empty(cq),                        "empty after pop type");

    free(cq);
}
// ---------------------------------------------------------------------------
// 10. randomised stress test
// ---------------------------------------------------------------------------
static void test_random_stress(void) {
    printf("test_random_stress\n");
    srand((unsigned)time(NULL));

    cq_t *cq = make_queue();

    // reference shadow queue (simple array)
    cqe_t shadow[4096];
    unsigned s_head = 0, s_tail = 0;
    unsigned s_n = 0;
    unsigned capacity = CQ_N - 1;

    int mismatch = 0;
    unsigned ops = 100000;

    for (unsigned i = 0; i < ops && !mismatch; i++) {
        int push = rand() & 1;

        if (push && s_n < capacity) {
            cqe_t v = (cqe_t)(rand() & 0xFF);
            shadow[s_head % 4096] = v;
            s_head++;
            s_n++;
            int rc = cq_push(cq, v);
            if (rc != CQ_OP_SUCCESS) { mismatch = 1; break; }
        } else if (!push && s_n > 0) {
            cqe_t expected = shadow[s_tail % 4096];
            s_tail++;
            s_n--;
            cqe_t got;
            int rc = cq_pop(cq, &got);
            if (rc != CQ_OP_SUCCESS || got != expected) { mismatch = 1; break; }
        }

        // invariant checks
        if (cq_nelem(cq) != s_n) { mismatch = 1; break; }
    }

    CHECK(!mismatch, "random stress: queue matches shadow over 100k ops");
    free(cq);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(void) {
    test_init();
    test_push_pop_single();
    test_fifo_order();
    test_boundary_failures();
    test_peek();
    test_push_pop_n();
    test_peek_n();
    test_wraparound();
    test_push_pop32();
    test_push_pop_type();
    test_random_stress();

    printf("\n%d / %d tests passed\n", test_passed, test_run);
    return test_passed == test_run ? 0 : 1;
}
