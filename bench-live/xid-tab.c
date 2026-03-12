#include <string.h>
#include <stdint.h>
#include <pthread.h>

#include "xid-tab.h"
#include "lib/hash.h"

pthread_mutex_t xid_tab_lock = PTHREAD_MUTEX_INITIALIZER;

// using FNV-1a hash for testing instead of modulo 
static unsigned fnv1a(const void *data, unsigned size) {
    const unsigned char *p = (const unsigned char *)data;
    unsigned hash = 2166136261u;  // FNV offset basis
    for (unsigned i = 0; i < size; i++) {
        hash ^= p[i];
        hash *= 16777619u;        // FNV prime
    }
    return hash % HASH_N;
}

static unsigned generic_hash(const struct hash_elem *e, void *aux) {
    (void)aux;
    struct client_async *c_async;
    memcpy(&c_async, e->elem, sizeof(void *));
    return fnv1a((const void *)&c_async->c.xid, sizeof(uint32_t));
}

static int generic_comp(const struct hash_elem *a,
                        const struct hash_elem *b, void *aux) {
    (void)aux;
    struct client_async *ca, *cb;
    memcpy(&ca, a->elem, a->elem_size);
    memcpy(&cb, b->elem, b->elem_size);

    return ca->c.xid != cb->c.xid;
}

static struct hash_tab xid_tab; 

void xid_tab_init() {
    hash_init (&xid_tab, generic_hash, generic_comp, NULL);
}

void xid_tab_insert(struct client_async *c_async) {
    struct hash_elem e;
    memcpy(e.elem, (uint8_t *)&c_async, sizeof(void *));
    e.elem_size = sizeof(void *); 
    pthread_mutex_lock(&xid_tab_lock);
    hash_insert(&xid_tab, &e);
    pthread_mutex_unlock(&xid_tab_lock);
}

struct client_async *xid_tab_lookup(uint32_t xid) {
    struct client_async c_dummy; 
    c_dummy.c.xid = xid;

    struct hash_elem e;
    
    struct client_async *dummy_ptr = &c_dummy;
    memcpy(e.elem, (uint8_t *)&dummy_ptr, sizeof(void *));
    e.elem_size = sizeof(void *); 
    
    pthread_mutex_lock(&xid_tab_lock);
    struct hash_elem *found_e = hash_find(&xid_tab, &e);

    if (!found_e) { 
        pthread_mutex_unlock(&xid_tab_lock);
        return NULL;
    }
    
    struct client_async *c_async;
    memcpy(&c_async, found_e->elem, sizeof(void *));
    pthread_mutex_unlock(&xid_tab_lock);

    return c_async;
}

// TODO: MAYBE don't use it or use it outside the loop it adds too much 
// latency
void xid_tab_remove(uint32_t xid) {
    struct client_async c_dummy; 
    c_dummy.c.xid = xid;

    struct hash_elem e;
    
    struct client_async *dummy_ptr = &c_dummy;
    memcpy(e.elem, (uint8_t *)&dummy_ptr, sizeof(void *));
    e.elem_size = sizeof(void *); 
    
    pthread_mutex_lock(&xid_tab_lock);
    hash_delete(&xid_tab, &e);
    pthread_mutex_unlock(&xid_tab_lock);
}
