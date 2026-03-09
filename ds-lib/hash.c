#include "hash.h"
// #include "stdio.h" // for local debug only, may not assume present on embedded system

// #define DEBUG

#define DEFINE_HASH_IMPL(SUFFIX, ELEM_MAX)                                   \
static void insert_elem_##SUFFIX(struct hash_tab_##SUFFIX *h,                \
    unsigned idx, struct hash_elem_##SUFFIX *new) {                          \
    h->occupied[idx] = 1;                                                    \
    h->buckets[idx].elem_size = new->elem_size;                              \
    for (unsigned i = 0; i < ELEM_MAX; i++)                                  \
        h->buckets[idx].elem[i] = new->elem[i];                              \
}                                                                            \
                                                                             \
static void replace_elem_##SUFFIX(struct hash_elem_##SUFFIX *old,            \
    struct hash_elem_##SUFFIX *new) {                                        \
    old->elem_size = new->elem_size;                                         \
    for (unsigned i = 0; i < ELEM_MAX; i++)                                  \
        old->elem[i] = new->elem[i];                                         \
}                                                                            \
                                                                             \
static void clear_elem_##SUFFIX(struct hash_tab_##SUFFIX *h, unsigned idx) { \
    h->occupied[idx] = 0;                                                    \
    h->buckets[idx].elem_size = 0;                                           \
    for (unsigned i = 0; i < ELEM_MAX; i++)                                  \
        h->buckets[idx].elem[i] = 0;                                         \
}                                                                            \
                                                                             \
int hash_init_##SUFFIX(struct hash_tab_##SUFFIX *h,                          \
    hash_hash_func_##SUFFIX *hash,                                           \
    hash_comp_func_##SUFFIX *comp, void *aux) {                              \
    h->capacity = HASH_N; h->hash = hash; h->comp = comp; h->aux = aux;      \
    hash_clear_##SUFFIX(h);                                                  \
    if (h->elem_cnt != 0) return HASH_OP_FAIL;                               \
    return HASH_OP_SUCCESS;                                                  \
}                                                                            \
                                                                             \
void hash_clear_##SUFFIX(struct hash_tab_##SUFFIX *h) {                      \
    for (unsigned i = 0; i < h->capacity; i++) clear_elem_##SUFFIX(h, i);    \
    h->elem_cnt = 0;                                                         \
}                                                                            \
                                                                             \
struct hash_elem_##SUFFIX *hash_find_##SUFFIX(                               \
    struct hash_tab_##SUFFIX *h, struct hash_elem_##SUFFIX *e) {             \
    unsigned h_idx = h->hash(e, h->aux) % h->capacity;                       \
    unsigned start_idx = h_idx;                                              \
    while (h->occupied[h_idx]) {                                             \
        if (h->comp(&h->buckets[h_idx], e, h->aux) == 0)                     \
            return &h->buckets[h_idx];                                       \
        h_idx = (h_idx + 1) % h->capacity;                                   \
        if (start_idx == h_idx) break;                                       \
    }                                                                        \
    return (struct hash_elem_##SUFFIX *)0;                                   \
}                                                                            \
                                                                             \
int hash_insert_##SUFFIX(struct hash_tab_##SUFFIX *h,                        \
    struct hash_elem_##SUFFIX *new) {                                        \
    if (h->capacity == h->elem_cnt) return HASH_OP_FAIL;                     \
    unsigned h_idx = h->hash(new, h->aux) % h->capacity;                     \
    unsigned orig_idx = h_idx;                                               \
    while (h->occupied[h_idx]) {                                             \
        if (h->comp(&h->buckets[h_idx], new, h->aux) == 0) {                 \
            replace_elem_##SUFFIX(&h->buckets[h_idx], new);                  \
            return HASH_OP_SUCCESS;                                          \
        }                                                                    \
        h_idx = (h_idx + 1) % h->capacity;                                   \
        if (h_idx == orig_idx) return HASH_OP_FAIL;                          \
    }                                                                        \
    insert_elem_##SUFFIX(h, h_idx, new); h->elem_cnt++;                      \
    return HASH_OP_SUCCESS;                                                  \
}                                                                            \
                                                                             \
int hash_replace_##SUFFIX(struct hash_tab_##SUFFIX *h,                       \
    struct hash_elem_##SUFFIX *new) {                                        \
    struct hash_elem_##SUFFIX *old_elem = hash_find_##SUFFIX(h, new);        \
    if (old_elem == 0) return HASH_OP_FAIL;                                  \
    replace_elem_##SUFFIX(old_elem, new);                                    \
    return HASH_OP_SUCCESS;                                                  \
}                                                                            \
                                                                             \
int hash_delete_##SUFFIX(struct hash_tab_##SUFFIX *h,                        \
    struct hash_elem_##SUFFIX *e) {                                          \
    unsigned h_idx = h->hash(e, h->aux) % h->capacity;                       \
    unsigned start_idx = h_idx;                                              \
    int found = 0;                                                           \
    while (h->occupied[h_idx]) {                                             \
        if (h->comp(&h->buckets[h_idx], e, h->aux) == 0) { found=1; break; } \
        h_idx = (h_idx+1) % h->capacity;                                     \
        if (h_idx==start_idx) break;                                         \
    }                                                                        \
    if (!found) return HASH_OP_FAIL;                                         \
    clear_elem_##SUFFIX(h, h_idx);                                           \
    h->elem_cnt--;                                                           \
    unsigned next_idx = (h_idx+1) % h->capacity;                             \
    start_idx = h_idx;                                                       \
    while (h->occupied[next_idx]) {                                          \
        unsigned expected_idx = h->hash(&h->buckets[next_idx], h->aux) % h->capacity;  \
        if (expected_idx != next_idx) {                                      \
            unsigned movable_idx = expected_idx;                             \
            while (movable_idx != next_idx) {                                \
                if (!h->occupied[movable_idx]) break;                        \
                movable_idx = (movable_idx+1) % h->capacity;                 \
            }                                                                \
            if (movable_idx != next_idx) {                                   \
                insert_elem_##SUFFIX(h, movable_idx, &h->buckets[next_idx]); \
                clear_elem_##SUFFIX(h, next_idx);                            \
            }                                                                \
        }                                                                    \
        next_idx = (next_idx+1) % h->capacity;                               \
        if (next_idx==start_idx) break;                                      \
    }                                                                        \
    return HASH_OP_SUCCESS;                                                  \
}                                                                            \
                                                                             \
unsigned int hash_size_##SUFFIX(struct hash_tab_##SUFFIX *h) {               \
    return h->elem_cnt; }                                                    \
int hash_empty_##SUFFIX(struct hash_tab_##SUFFIX *h) {                       \
    return h->elem_cnt == 0; }                                  

DEFINE_HASH_IMPL(mac, HASH_ELEM_MAX)
DEFINE_HASH_IMPL(ip, HASH_ELEM_IP_SIZE)
