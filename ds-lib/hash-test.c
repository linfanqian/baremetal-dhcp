#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hash.h"

// ---------------------------------------------------------------------------
// tiny test harness
// ---------------------------------------------------------------------------
static int tests_run = 0, tests_passed = 0;
#define CHECK(cond, msg) do {               \
    tests_run++;                            \
    if (cond) { tests_passed++; }           \
    else { printf("  FAIL [%s:%d]: %s\n",   \
            __FILE__, __LINE__, msg); }     \
} while(0)

// ---------------------------------------------------------------------------
// FNV-1a hash — much better distribution than modulo, still very cheap
// Works on raw bytes so it naturally handles any elem_size
// ---------------------------------------------------------------------------
static unsigned fnv1a(const void *data, unsigned size) {
    const unsigned char *p = (const unsigned char *)data;
    unsigned hash = 2166136261u;  // FNV offset basis
    for (unsigned i = 0; i < size; i++) {
        hash ^= p[i];
        hash *= 16777619u;        // FNV prime
    }
    return hash % HASH_N;
}

static void print_hash_table(const struct hash_tab *h) {
    for (unsigned j = 0; j < h->capacity; j++) {
        if (h->occupied[j])
            printf("%d, ", *(int *)h->buckets[j].elem);
        else 
            printf("[EMPTY], ");
    }
    printf("\n");
}

// ---------------------------------------------------------------------------
// generic hash / comp functions operating on elem_size bytes
// ---------------------------------------------------------------------------
static unsigned generic_hash(const struct hash_elem *e, void *aux) {
    (void)aux;
    return fnv1a(e->elem, e->elem_size);
}

static int generic_comp(const struct hash_elem *a,
                        const struct hash_elem *b, void *aux) {
    (void)aux;
    if (a->elem_size != b->elem_size) return 1;
    return memcmp(a->elem, b->elem, a->elem_size);
}

// ---------------------------------------------------------------------------
// helpers to build hash_elem from arbitrary data
// ---------------------------------------------------------------------------
static struct hash_elem make_elem(const void *data, unsigned size) {
    struct hash_elem e;
    memset(&e, 0, sizeof e);
    e.elem_size = size;
    memcpy(e.elem, data, size);
    return e;
}

#define ELEM_U32(x)  make_elem(&(unsigned int){x}, sizeof(unsigned int))
#define ELEM_MAC(m)  make_elem(m, 6)
#define ELEM_IP(ip)  make_elem(&(unsigned int){ip}, sizeof(unsigned int))

// ---------------------------------------------------------------------------
// shadow hash table (linear scan array) for correctness verification
// ---------------------------------------------------------------------------
#define SHADOW_MAX HASH_N
typedef struct {
    unsigned char data[HASH_ELEM_MAX];
    unsigned size;
    int used;
} shadow_entry_t;

typedef struct {
    shadow_entry_t entries[SHADOW_MAX];
    unsigned count;
} shadow_tab_t;

static void shadow_clear(shadow_tab_t *s) {
    memset(s, 0, sizeof *s);
}

static shadow_entry_t *shadow_find(shadow_tab_t *s, const struct hash_elem *e) {
    for (unsigned i = 0; i < SHADOW_MAX; i++) {
        if (s->entries[i].used &&
            s->entries[i].size == e->elem_size &&
            memcmp(s->entries[i].data, e->elem, e->elem_size) == 0)
            return &s->entries[i];
    }
    return NULL;
}

static int shadow_insert(shadow_tab_t *s, const struct hash_elem *e) {
    shadow_entry_t *existing = shadow_find(s, e);
    if (existing) {
        memcpy(existing->data, e->elem, e->elem_size);
        return 1;
    }
    for (unsigned i = 0; i < SHADOW_MAX; i++) {
        if (!s->entries[i].used) {
            s->entries[i].used = 1;
            s->entries[i].size = e->elem_size;
            memcpy(s->entries[i].data, e->elem, e->elem_size);
            s->count++;
            return 1;
        }
    }
    return 0;
}

static int shadow_delete(shadow_tab_t *s, const struct hash_elem *e) {
    shadow_entry_t *entry = shadow_find(s, e);
    if (!entry) return 0;
    memset(entry, 0, sizeof *entry);
    s->count--;
    return 1;
}

// ---------------------------------------------------------------------------
// 1. init sanity
// ---------------------------------------------------------------------------
static void test_init(void) {
    printf("test_init\n");
    struct hash_tab h;
    int rc = hash_init(&h, generic_hash, generic_comp, NULL);
    CHECK(rc == HASH_OP_SUCCESS,  "init succeeds");
    CHECK(hash_empty(&h),         "empty after init");
    CHECK(hash_size(&h) == 0,     "size == 0 after init");
    CHECK(h.capacity == HASH_N,   "capacity == HASH_N");
}

// ---------------------------------------------------------------------------
// 2. uint32 insert / find / delete
// ---------------------------------------------------------------------------
static void test_uint32(void) {
    printf("test_uint32\n");
    struct hash_tab h;
    hash_init(&h, generic_hash, generic_comp, NULL);

    struct hash_elem e1 = ELEM_U32(0xDEADBEEF);
    struct hash_elem e2 = ELEM_U32(0xCAFEBABE);

    CHECK(hash_insert(&h, &e1) == HASH_OP_SUCCESS, "insert u32 #1");
    CHECK(hash_insert(&h, &e2) == HASH_OP_SUCCESS, "insert u32 #2");
    CHECK(hash_size(&h) == 2,                      "size == 2");

    CHECK(hash_find(&h, &e1) != NULL, "find u32 #1");
    CHECK(hash_find(&h, &e2) != NULL, "find u32 #2");

    struct hash_elem missing = ELEM_U32(0x12345678);
    CHECK(hash_find(&h, &missing) == NULL, "find missing returns NULL");

    CHECK(hash_delete(&h, &e1) == HASH_OP_SUCCESS, "delete u32 #1");
    CHECK(hash_find(&h, &e1) == NULL,              "find after delete returns NULL");
    CHECK(hash_size(&h) == 1,                      "size == 1 after delete");
}

// ---------------------------------------------------------------------------
// 3. MAC address (6 bytes)
// ---------------------------------------------------------------------------
static void test_mac(void) {
    printf("test_mac\n");
    struct hash_tab h;
    hash_init(&h, generic_hash, generic_comp, NULL);

    unsigned char mac1[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    unsigned char mac2[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    unsigned char mac3[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};

    struct hash_elem e1 = ELEM_MAC(mac1);
    struct hash_elem e2 = ELEM_MAC(mac2);
    struct hash_elem e3 = ELEM_MAC(mac3);

    CHECK(hash_insert(&h, &e1) == HASH_OP_SUCCESS, "insert MAC #1");
    CHECK(hash_insert(&h, &e2) == HASH_OP_SUCCESS, "insert MAC #2");
    CHECK(hash_insert(&h, &e3) == HASH_OP_SUCCESS, "insert MAC #3");
    CHECK(hash_size(&h) == 3,                      "size == 3");

    CHECK(hash_find(&h, &e1) != NULL, "find MAC #1");
    CHECK(hash_find(&h, &e2) != NULL, "find MAC #2");
    CHECK(hash_find(&h, &e3) != NULL, "find MAC #3");

    CHECK(hash_delete(&h, &e2) == HASH_OP_SUCCESS, "delete MAC #2");
    CHECK(hash_find(&h, &e2) == NULL,              "find MAC #2 after delete");
    // probe chain still intact
    CHECK(hash_find(&h, &e1) != NULL, "find MAC #1 after deleting #2");
    CHECK(hash_find(&h, &e3) != NULL, "find MAC #3 after deleting #2");
}

// ---------------------------------------------------------------------------
// 4. IP address — insert / replace / find
// ---------------------------------------------------------------------------
static void test_ip(void) {
    printf("test_ip\n");
    struct hash_tab h;
    hash_init(&h, generic_hash, generic_comp, NULL);

    // 192.168.1.x encoded as uint32
    struct hash_elem e1 = ELEM_IP(0xC0A80101);  // 192.168.1.1
    struct hash_elem e2 = ELEM_IP(0xC0A80102);  // 192.168.1.2
    struct hash_elem e3 = ELEM_IP(0xC0A80103);  // 192.168.1.3

    hash_insert(&h, &e1);
    hash_insert(&h, &e2);
    hash_insert(&h, &e3);

    // re-insert same IP (replace)
    struct hash_elem e1_dup = ELEM_IP(0xC0A80101);
    CHECK(hash_insert(&h, &e1_dup) == HASH_OP_SUCCESS, "re-insert same IP succeeds");
    CHECK(hash_size(&h) == 3,                          "size unchanged after re-insert");

    CHECK(hash_find(&h, &e1) != NULL, "find IP 1");
    CHECK(hash_find(&h, &e2) != NULL, "find IP 2");
    CHECK(hash_find(&h, &e3) != NULL, "find IP 3");
}

// ---------------------------------------------------------------------------
// 5. collision stress — force collisions by using values that hash to same slot
// ---------------------------------------------------------------------------
static void test_collisions(void) {
    printf("test_collisions\n");
    struct hash_tab h;
    hash_init(&h, generic_hash, generic_comp, NULL);

    // insert enough sequential IPs to force collisions
    unsigned n = HASH_N / 2;
    for (unsigned i = 0; i < n; i++) {
        struct hash_elem e = ELEM_U32(i);
        CHECK(hash_insert(&h, &e) == HASH_OP_SUCCESS, "insert under load");
    }
    CHECK(hash_size(&h) == n, "size correct after bulk insert");

    // verify all findable
    int all_found = 1;
    for (unsigned i = 0; i < n; i++) {
        struct hash_elem e = ELEM_U32(i);
        if (!hash_find(&h, &e)) { all_found = 0; break; }
    }
    CHECK(all_found, "all elements findable after bulk insert");

    // delete every other one, verify probe chains intact
    for (unsigned i = 0; i < n; i += 2) {
        struct hash_elem e = ELEM_U32(i);
        hash_delete(&h, &e);
    }
    int chain_ok = 1;
    for (unsigned i = 1; i < n; i += 2) {
        struct hash_elem e = ELEM_U32(i);
        if (!hash_find(&h, &e)) { 
                printf("error: cannot find %d\n", i);
                // print_hash_table(&h);
                chain_ok = 0; break; 
        }
    }
    CHECK(chain_ok, "odd elements still findable after deleting evens");
}

static struct hash_elem shadow_pick(shadow_tab_t *s, unsigned elem_size) {
    // collect occupied indices
    unsigned indices[SHADOW_MAX];
    unsigned cnt = 0;
    for (unsigned i = 0; i < SHADOW_MAX; i++)
        if (s->entries[i].used) indices[cnt++] = i;

    struct hash_elem e;
    memset(&e, 0, sizeof e);
    e.elem_size = elem_size;
    if (cnt > 0) {
        shadow_entry_t *se = &s->entries[indices[rand() % cnt]];
        memcpy(e.elem, se->data, se->size);
    }
    return e;
}

static struct hash_elem random_elem(unsigned elem_size) {
    struct hash_elem e;
    memset(&e, 0, sizeof e);
    e.elem_size = elem_size;
    for (unsigned i = 0; i < elem_size; i++)
        e.elem[i] = (unsigned char)(rand() & 0xFF);
    return e;
}
// ---------------------------------------------------------------------------
// 6. random stress test against shadow table — parameterized by elem_size
// ---------------------------------------------------------------------------
static void stress_test(const char *label, unsigned elem_size, unsigned ops) {
    printf("stress_test: %s (%u byte elems, %u ops)\n", label, elem_size, ops);

    struct hash_tab h;
    shadow_tab_t shadow;
    hash_init(&h, generic_hash, generic_comp, NULL);
    shadow_clear(&shadow);

    int mismatch = 0;
    int num_insert = 0, num_find = 0, num_delete = 0;

    for (unsigned op = 0; op < ops && !mismatch; op++) {
        // random element within elem_size bytes
        
        int action = rand() % 3;  // 0=insert, 1=find, 2=delete
        
        // assume a maximum fill ratio of 4/5 (proably too high but we cannot expand loll)
        if (action == 0 && hash_size(&h) < HASH_N / 5 * 4) {
            num_insert++;
            struct hash_elem e = random_elem(elem_size);
            int hr = hash_insert(&h, &e);
            shadow_insert(&shadow, &e);
            (void)hr;
        } else if (action == 1) {
            num_find++;
           // 2/3 chance pick known key, 1/3 random
            struct hash_elem e = (shadow.count > 0 && rand() % 3 != 0)
                ? shadow_pick(&shadow, elem_size)
                : random_elem(elem_size);
            int hr = (hash_find(&h, &e) != NULL);
            int sr = (shadow_find(&shadow, &e) != NULL);
            if (hr != sr) {
                printf("  MISMATCH find: hash=%d shadow=%d\n", hr, sr);
                print_hash_table(&h);
                mismatch = 1;
            }
        } else {
            num_delete++;
           // 2/3 chance pick known key, 1/3 random
            struct hash_elem e = (shadow.count > 0 && rand() % 3 != 0)
                ? shadow_pick(&shadow, elem_size)
                : random_elem(elem_size);
            int hr = hash_delete(&h, &e);
            int sr = shadow_delete(&shadow, &e);
            if (hr != sr) {
                printf("  MISMATCH delete: hash=%d shadow=%d\n", hr, sr);
                print_hash_table(&h);
                mismatch = 1;
            }
        }

        // size invariant
        if (hash_size(&h) != shadow.count) {
            printf("  SIZE MISMATCH: hash=%u shadow=%u\n",
                   hash_size(&h), shadow.count);
            mismatch = 1;
        }
    }
    printf("    SUMMARY: commited %d insertion, %d find and %d deletion\n", num_insert, num_find, num_delete);
    printf("    SUMMARY: current hash table size after stress test %d / %d\n", h.elem_cnt, h.capacity);

    CHECK(!mismatch, "shadow matches hash table throughout stress test");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(void) {
    srand((unsigned)time(NULL));

    test_init();
    test_uint32();
    test_mac();
    test_ip();
    test_collisions();

    // stress test with different data sizes
    stress_test("uint32",  sizeof(unsigned int), 10000);
    stress_test("MAC",     6,                10000);
    stress_test("IP",      sizeof(unsigned int), 10000);
    stress_test("mixed8",  8,                10000);

    printf("\n%d / %d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
