#ifndef DHCP_HASHMAP_H
#define DHCP_HASHMAP_H

/*
 * TABLE-mode IP lease pool.
 * Tracks per-client leases in a caller-provided array, keyed by MAC address.
 */

#include <stdint.h>
#include <stdbool.h>

#define HASH_ELEM_MAX 14
#define HASH_N 4096       
#include "hash.h"

/* One lease entry, mapping a client MAC to an IP address. */
typedef struct {
    uint8_t mac_address[6];     /* MAC address */
    uint32_t ip_address;        /* Leased IP address */
    uint32_t expire_time;       /* When the lease expire (absolute)*/
}__attribute__((packed)) dhcp_hash_elem;

/* TABLE-mode lease pool */
typedef struct {
    struct hash_tab leases;
    uint32_t next_ip;
} dhcp_hashpool_t;

void dhcp_hashpool_init(dhcp_hashpool_t *pool, uint32_t pool_start);
uint32_t dhcp_hashpool_find_available_ip(dhcp_hashpool_t *pool, uint32_t pool_start, uint32_t pool_end, uint8_t *mac, uint32_t cur_time);
dhcp_hash_elem *dhcp_hashpool_find_lease(dhcp_hashpool_t *pool, uint8_t *mac);
bool dhcp_hashpool_alloc_lease(dhcp_hashpool_t *pool, uint32_t ip, uint8_t *mac, uint32_t lease_time, uint32_t cur_time);
void dhcp_hashpool_cleanup_expire_lease(dhcp_hashpool_t *pool, uint32_t cur_time); 

#endif /* DHCP_HASHMAP_H */
