#include "dhcp_hashmap.h"
#include "dhcp_compat.h"

static unsigned dhcp_mac_hash(const struct hash_elem *e, void *aux) {
    unsigned h = 2166136261u;
    for (int i = 0; i < 6; i++) {
        h ^= e->elem[i];
        h *= 16777619u;
    }
    return h % HASH_N;  // hash.c applies % capacity
}

static int dhcp_mac_comp(const struct hash_elem *a,
                          const struct hash_elem *b, void *aux) {
    for (int i = 0; i < 6; i++) {
        if (a->elem[i] != b->elem[i])
            return (int)a->elem[i] - (int)b->elem[i];
    }
    return 0;
}

void dhcp_hashpool_init(dhcp_hashpool_t *pool, uint32_t pool_start) {
    hash_init(&pool->leases, dhcp_mac_hash, dhcp_mac_comp, (void *)0);
    pool->next_ip = pool_start;
}

/* Find an available IP address from the pool */
uint32_t dhcp_hashpool_find_available_ip(dhcp_hashpool_t *pool,
                                          uint32_t pool_start, uint32_t pool_end,
                                          uint8_t *mac, uint32_t cur_time) {
    /* Check if MAC already has a lease */
    dhcp_hash_elem *existing = dhcp_hashpool_find_lease(pool, mac);
    if (existing)
        return existing->ip_address;

    uint32_t range_size = pool_end - pool_start + 1;
    for (uint32_t count = 0; count < range_size; count++) {
        uint32_t ip = pool->next_ip;
        pool->next_ip = pool_start + ((ip - pool_start + 1) % range_size);

        uint8_t found = 0;
        for (uint16_t i = 0; i < HASH_N; i++) {
            if (pool->leases.occupied[i]) {
                dhcp_hash_elem *e = (dhcp_hash_elem *)pool->leases.buckets[i].elem;
                if (e->ip_address == ip) {
                    found = 1;
                    break;
                }
            }
        }
        if (!found)
            return ip;   
    }
    return 0; /* No available IP */
}

/* Find a lease by MAC address */
dhcp_hash_elem *dhcp_hashpool_find_lease(dhcp_hashpool_t *pool, uint8_t *mac) {
    struct hash_elem key;
    key.elem_size = 6;
    kmemcpy(key.elem, mac, 6);
    struct hash_elem *result = hash_find(&pool->leases, &key);
    if (!result)
        return (dhcp_hash_elem *)0;
    return (dhcp_hash_elem *)result->elem;
}

/* Allocate a new lease */
bool dhcp_hashpool_alloc_lease(dhcp_hashpool_t *pool, uint32_t ip, uint8_t *mac,
                                uint32_t lease_time, uint32_t cur_time) {
    // check if a lease exists with this MAC
    dhcp_hash_elem *existing = dhcp_hashpool_find_lease(pool, mac);
    if (existing) {
        if (existing->ip_address != ip) {
            return false;
        }
        else {
            existing->expire_time = cur_time + lease_time;
            return true;
        }
    } 

    if (hash_size(&pool->leases) >= HASH_N)
        return false;

    struct hash_elem new_elem;
    new_elem.elem_size = HASH_ELEM_MAX;
    dhcp_hash_elem *e = (dhcp_hash_elem *)new_elem.elem;
    kmemcpy(e->mac_address, mac, 6);
    e->ip_address  = ip;
    e->expire_time = cur_time + lease_time;

    hash_insert(&pool->leases, &new_elem);
    return true;
}

void dhcp_hashpool_cleanup_expire_lease(dhcp_hashpool_t *pool, uint32_t cur_time) {
    uint8_t expired_macs[32][6];
    uint8_t count = 0;

    for (uint16_t i = 0; i < HASH_N && count < 32; i++) {
        if (pool->leases.occupied[i]) {
            dhcp_hash_elem *e = (dhcp_hash_elem *)pool->leases.buckets[i].elem;
            if (e->expire_time <= cur_time)
                kmemcpy(expired_macs[count++], e->mac_address, 6);
        }
    }

    for (uint8_t j = 0; j < count; j++) {
        struct hash_elem key;
        key.elem_size = 6;
        kmemcpy(key.elem, expired_macs[j], 6);
        hash_delete(&pool->leases, &key);
    }
}

