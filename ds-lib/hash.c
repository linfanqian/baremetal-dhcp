#include "hash.h"
// #include "stdio.h" // for local debug only, may not assume present on embedded system

static void insert_elem(struct hash_tab *h, unsigned h_idx, struct hash_elem *new) {
   h->occupied[h_idx] = 1;
   h->buckets[h_idx].elem_size = new->elem_size; 
   for (unsigned i = 0; i < HASH_ELEM_MAX; i++)
       h->buckets[h_idx].elem[i] = new->elem[i];
}

static void replace_elem(struct hash_tab *h, struct hash_elem *old, struct hash_elem *new) {
   old->elem_size = new->elem_size; 
   for (unsigned i = 0; i < HASH_ELEM_MAX; i++)
       old->elem[i] = new->elem[i];
}

static void clear_elem(struct hash_tab *h, unsigned h_idx) {
   h->occupied[h_idx] = 0;
   h->buckets[h_idx].elem_size = 0;
   for (unsigned i = 0; i < HASH_ELEM_MAX; i++)
       h->buckets[h_idx].elem[i] = 0;
}

static void rehash(struct hash_tab *h) {
    // without malloc / kalloc, the current system cannot really support rehashing
    // the only way is to manually check and migrate to a larger one if needed
    // but that also requires modifying macro in the mid air so let's call it a day
    // for now
}

int hash_init(struct hash_tab *h,
               hash_hash_func *hash, hash_comp_func *comp, void *aux)
{
    hash_clear(h);
    h->hash = hash;
    h->comp = comp;
    h->aux = aux;
    h->capacity = HASH_N; 
    
    if (!hash_empty (h)) return HASH_OP_FAIL;
    return HASH_OP_SUCCESS;
}

void hash_clear(struct hash_tab *h)
{
    for (unsigned i = 0; i < h->capacity; i++)
        clear_elem(h, i);

  h->elem_cnt = 0;
}

int hash_insert(struct hash_tab *h, struct hash_elem *new)
{
   if (h->capacity == h->elem_cnt) 
       return HASH_OP_FAIL;

   unsigned h_idx = h->hash(new, h->aux) % h->capacity;
   unsigned orig_idx = h_idx;

   while (h->occupied[h_idx]) {
       if (h->comp(&h->buckets[h_idx], new, h->aux) == 0) {
           // element already exists
           replace_elem(h, &h->buckets[h_idx], new);
           return HASH_OP_SUCCESS;
        }
       h_idx = (h_idx + 1) % h->capacity;
       if (h_idx == orig_idx) return HASH_OP_FAIL; 
   }
#ifdef DEBUG
   printf("inserting %d (expected idx %d) to %d\n",
           *(int *)new->elem, 
           h->hash(new, h->aux) % h->capacity,
           h_idx);
#endif

   insert_elem(h, h_idx, new);
   h->elem_cnt++;
   // rehash(h);
   return HASH_OP_SUCCESS;
}

/* Inserts NEW into hash table H, replacing any equal element
   already in the table, which is returned. */
int hash_replace(struct hash_tab *h, struct hash_elem *new)
{
    struct hash_elem *old_elem = hash_find(h, new);
    if (old_elem != 0) {// found 
        replace_elem(h, old_elem, new);
        return HASH_OP_SUCCESS;
    }
    
    return HASH_OP_FAIL;
}

/* Finds and returns an element equal to E in hash table H, or a
   null pointer if no equal element exists in the table. */
struct hash_elem *
hash_find(struct hash_tab *h, struct hash_elem *e)
{
   unsigned h_idx = h->hash(e, h->aux) % h->capacity;
   unsigned start_idx = h_idx;

    while (h->occupied[h_idx]) {
       if (h->comp(&h->buckets[h_idx], e, h->aux) == 0) {
           // find element
           return &h->buckets[h_idx];
        }
        h_idx = (h_idx + 1) % h->capacity;
        if (start_idx == h_idx) break;
   }
#ifdef DEBUG
    printf("expected to find %d from %d through %d, failed\n",
            *(int *)e->elem, start_idx, h_idx);
#endif
    return 0;
}

int hash_delete(struct hash_tab *h, struct hash_elem *e)
{
   unsigned h_idx = h->hash(e, h->aux) % h->capacity;
   unsigned start_idx = h_idx;
   int found = 0;

   while (h->occupied[h_idx]) {
        if (h->comp(&h->buckets[h_idx], e, h->aux) == 0) {
           found = 1;
           break;
        }
       h_idx = (h_idx + 1) % h->capacity;
       if (start_idx == h_idx) break;
   }
   // not found
   if (!found) 
       return HASH_OP_FAIL;

    clear_elem(h, h_idx);
    h->elem_cnt--;

    unsigned next_idx = (h_idx + 1) % h->capacity;
    start_idx = h_idx;
    while (h->occupied[next_idx]) {
        // assume aux is some global state
        unsigned expected_idx = h->hash(&h->buckets[next_idx], h->aux) % h->capacity;
        
        if (expected_idx != next_idx) {
            unsigned movable_idx = expected_idx;
            while (movable_idx != next_idx) {
                if (!h->occupied[movable_idx]) break;
                movable_idx = (movable_idx + 1) % h->capacity;
            }
            if (movable_idx != next_idx) {
                insert_elem(h, movable_idx, &h->buckets[next_idx]);
                clear_elem(h, next_idx);
#ifdef DEBUG
                printf("moving %d (expected idx %d) from current idx %d to new idx %d\n)", 
                    *(int *)h->buckets[movable_idx].elem, expected_idx,
                    next_idx, movable_idx);
#endif
            }
        }

        next_idx = (next_idx + 1) % h->capacity;
        if (next_idx == start_idx) break;
    }

    return HASH_OP_SUCCESS;
}

/* Returns the number of elements in H. */
unsigned int hash_size(struct hash_tab *h) { return h->elem_cnt; }
/* Returns true if H contains no elements, false otherwise. */
int hash_empty(struct hash_tab *h) { return h->elem_cnt == 0; }

