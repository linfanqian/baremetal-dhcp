#ifndef __HASH_H__
#define __HASH_H__

#ifndef HASH_N
#   define HASH_N 8          // may be defined elsewhere by the client?
#endif

#ifndef HASH_ELEM_MAX
#   define HASH_ELEM_MAX 8      // covers MAC, IPv4, small strings
#endif

// in case there is a problem of user-defined HASH_N
#ifdef __cplusplus
static_assert((HASH_N & (HASH_N - 1)) == 0, "HASH_N must be a power of 2");
#else
_Static_assert((HASH_N & (HASH_N - 1)) == 0, "HASH_N must be a power of 2");
#endif

// TODO: potentially meta-data here?
struct hash_elem {
    unsigned char elem[HASH_ELEM_MAX];
    unsigned elem_size;
};

enum {
    HASH_OP_FAIL = 0,
    HASH_OP_SUCCESS = 1,
};

typedef unsigned hash_hash_func (const struct hash_elem *e, void *aux);
typedef int hash_comp_func (const struct hash_elem *a,
                            const struct hash_elem *b, void *aux);

/* Hash table. */
struct hash_tab 
  {
    struct hash_elem buckets[HASH_N];   /* actual tabular for data storage */
    unsigned char occupied[HASH_N];     /* random bitmap to track occupancy */
    unsigned int elem_cnt;              /* Number of elements in table. */
    unsigned int capacity;              /* Max number of elements */
    hash_hash_func *hash;               /* Hash function. */
    hash_comp_func *comp;               /* Comparison function. */
    void *aux;                          /* Auxiliary data for `hash' and `less'. */
  };

/* Basic life cycle. */
int hash_init (struct hash_tab *, hash_hash_func *, hash_comp_func *, void *aux);
void hash_clear (struct hash_tab *);
struct hash_elem *hash_find (struct hash_tab *, struct hash_elem *);

/* Search, insertion, deletion. */
int hash_insert (struct hash_tab *, struct hash_elem *);
int hash_replace (struct hash_tab *, struct hash_elem *);
int hash_delete (struct hash_tab *, struct hash_elem *);

/* Information. */
unsigned int hash_size (struct hash_tab *);
int hash_empty (struct hash_tab *);

#endif
