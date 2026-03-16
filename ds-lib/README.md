# ds-lib

Shared data structure library used by both the bare-metal DHCP server (on the Raspberry Pi) and the bench-live benchmark (on the host machine). Designed to work in both embedded (no stdlib) and hosted environments.

## Files

| File | Purpose |
|---|---|
| `hash.h` / `hash.c` | Open-addressing hash table, macro-generated for different key sizes |
| `cqueue.h` / `cqueue.c` | Lock-free circular FIFO queue |
| `hash-test.c` | Unit + stress tests for the hash table |
| `cqueue-test.c` | Unit tests for the circular queue |

---

## hash — Open-Addressing Hash Table

A fixed-size hash table using open addressing (linear probing). The implementation is generated via C macros so the same code works for different key types (MAC addresses, IPv4 addresses, etc.) without dynamic allocation.

### Key Parameters (`hash.h`)

| Parameter | Default | Meaning |
|---|---|---|
| `HASH_N` | 2048 | Number of buckets. **Must be a power of 2.** Controls capacity and collision rate. Reduce for embedded targets with limited memory. |
| `HASH_ELEM_MAX` | 14 | Max key size in bytes for the `mac` variant (covers 6-byte MAC + padding). |
| `HASH_ELEM_IP_SIZE` | 4 | Key size in bytes for the `ip` variant (IPv4 = 4 bytes). |

`HASH_N` can be overridden at compile time:
```c
gcc -DHASH_N=512 ...
```

### Generated Types

The macro `DEFINE_HASH_TYPES(SUFFIX, ELEM_MAX)` generates a complete hash table type and its functions for a given key size. Two variants are pre-defined:

| Variant | Key size | Use case |
|---|---|---|
| `hash_tab_mac` | 14 bytes (`HASH_ELEM_MAX`) | MAC addresses, uint32 XIDs, small keys |
| `hash_tab_ip` | 4 bytes (`HASH_ELEM_IP_SIZE`) | IPv4 addresses |

### API

All functions follow the pattern `hash_<op>_<suffix>`. Example using the `mac` variant:

```c
struct hash_tab_mac h;

// Initialize — provide your own hash and comparison functions
hash_init_mac(&h, my_hash_fn, my_comp_fn, NULL);

// Insert a key
struct hash_elem_mac e;
e.elem_size = 6;
memcpy(e.elem, some_mac, 6);
hash_insert_mac(&h, &e);   // returns HASH_OP_SUCCESS or HASH_OP_FAIL

// Find a key — returns pointer to stored element, or NULL if not found
struct hash_elem_mac *found = hash_find_mac(&h, &e);

// Delete a key
hash_delete_mac(&h, &e);   // returns HASH_OP_SUCCESS or HASH_OP_FAIL

// Replace (update) an existing key
hash_replace_mac(&h, &e);

// Utility
hash_size_mac(&h);    // number of elements currently stored
hash_empty_mac(&h);   // 1 if empty
hash_clear_mac(&h);   // remove all elements
```

### Return values

| Value | Meaning |
|---|---|
| `HASH_OP_SUCCESS` (1) | Operation succeeded |
| `HASH_OP_FAIL` (0) | Operation failed (table full, key not found, etc.) |

---

## cqueue — Lock-Free Circular FIFO Queue

A simple circular buffer queue. Designed for single-producer / single-consumer use without locks (one thread mutates `head`, another mutates `tail`).

### Key Parameter (`cqueue.h`)

| Parameter | Default | Meaning |
|---|---|---|
| `CQ_N` | 256 | Queue capacity in bytes. The queue holds up to `CQ_N - 1` bytes at a time. Can be overridden at compile time with `-DCQ_N=512`. |

The element type `cqe_t` is `unsigned char` — the queue stores raw bytes. Use the `cq_push_type` / `cq_pop_type` macros to push/pop arbitrary structs.

### API

```c
cq_t q;
cq_init(&q);           // initialize queue

// Push / pop single bytes
cq_push(&q, byte);
cq_pop(&q, &byte);

// Push / pop arbitrary types (macro wrappers around cq_push_n / cq_pop_n)
uint32_t val = 42;
cq_push_type(&q, val);
cq_pop_type(&q, val);

// Push / pop n bytes
cq_push_n(&q, data_ptr, n);
cq_pop_n(&q, data_ptr, n);

// Convenience for int32
cq_push32(&q, x);
cq_pop32(&q, &x);

// Peek without consuming
cq_peek(&q, &byte);
cq_peek_n(&q, buf, n);

// Status
cq_empty(&q);          // 1 if empty
cq_full(&q);           // 1 if full
cq_nelem(&q);          // number of bytes currently in queue
cq_nspace(&q);         // number of bytes of free space
cq_ok(&q);             // 1 if queue is in a valid state
```

---

## Running Tests

```bash
# Hash table tests (unit + stress)
gcc hash-test.c hash.c -o hash-test && ./hash-test

# Circular queue tests
gcc cqueue-test.c cqueue.c -o cq-test && ./cq-test
```

Or from `bench-live`:
```bash
make hash-test && ./hash-test
make cq-test   && ./cq-test
```
