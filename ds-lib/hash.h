#ifndef __HASH_H__
#define __HASH_H__

#ifndef HASH_N
#   define HASH_N 2048          // may be defined elsewhere by the client?
#endif

#ifndef HASH_ELEM_MAX
#   define HASH_ELEM_MAX 14      // covers MAC, IPv4, small strings
#endif

#ifndef HASH_ELEM_IP_SIZE
#   define HASH_ELEM_IP_SIZE 4      // covers MAC, IPv4, small strings
#endif

// in case there is a problem of user-defined HASH_N
#ifdef __cplusplus
static_assert((HASH_N & (HASH_N - 1)) == 0, "HASH_N must be a power of 2");
#else
_Static_assert((HASH_N & (HASH_N - 1)) == 0, "HASH_N must be a power of 2");
#endif

enum {
    HASH_OP_FAIL = 0,
    HASH_OP_SUCCESS = 1,
};

// TODO: potentially meta-data for hash_elem_##SUFFIX?
/* Generate typed struct pair + function declarations for a given element size */
#define DEFINE_HASH_TYPES(SUFFIX, ELEM_MAX)                                  \
struct hash_elem_##SUFFIX {                                                  \
    unsigned char elem[ELEM_MAX];                                            \
    unsigned      elem_size;                                                 \
};                                                                           \
                                                                             \
typedef unsigned hash_hash_func_##SUFFIX(                                    \
    const struct hash_elem_##SUFFIX *e, void *aux);                          \
typedef int hash_comp_func_##SUFFIX(                                         \
    const struct hash_elem_##SUFFIX *a,                                      \
    const struct hash_elem_##SUFFIX *b, void *aux);                          \
                                                                             \
struct hash_tab_##SUFFIX {                                                   \
    struct hash_elem_##SUFFIX  buckets[HASH_N];                              \
    unsigned char              occupied[HASH_N];                             \
    unsigned int               elem_cnt;                                     \
    unsigned int               capacity;                                     \
    hash_hash_func_##SUFFIX   *hash;                                         \
    hash_comp_func_##SUFFIX   *comp;                                         \
    void                      *aux;                                          \
};                                                                           \
                                                                             \
int hash_init_##SUFFIX(struct hash_tab_##SUFFIX *,                           \
    hash_hash_func_##SUFFIX *, hash_comp_func_##SUFFIX *, void *);           \
void hash_clear_##SUFFIX(struct hash_tab_##SUFFIX *);                        \
struct hash_elem_##SUFFIX *hash_find_##SUFFIX(                               \
    struct hash_tab_##SUFFIX *, struct hash_elem_##SUFFIX *);                \
                                                                             \
int hash_insert_##SUFFIX(struct hash_tab_##SUFFIX *,                         \
    struct hash_elem_##SUFFIX *);                                            \
int hash_replace_##SUFFIX(struct hash_tab_##SUFFIX *,                        \
    struct hash_elem_##SUFFIX *);                                            \
int hash_delete_##SUFFIX(struct hash_tab_##SUFFIX *,                         \
    struct hash_elem_##SUFFIX *);                                            \
                                                                             \
unsigned int hash_size_##SUFFIX(struct hash_tab_##SUFFIX *);                 \
int hash_empty_##SUFFIX(struct hash_tab_##SUFFIX *);


DEFINE_HASH_TYPES(mac, HASH_ELEM_MAX)      /* hash_tab_mac, 14-byte elements */
DEFINE_HASH_TYPES(ip, HASH_ELEM_IP_SIZE)   /* hash_tab_ip, 4-byte elements  */

#endif
