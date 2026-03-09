#ifndef DHCP_HASHMAP_H
#define DHCP_HASHMAP_H

/*
 * HASHMAP-mode IP lease pool.
 * Tracks per-client leases in a caller-provided array, keyed by MAC address.
 */

#include <stdint.h>
#include <stdbool.h>   
#include "hash.h"

/* One lease entry, mapping a client MAC to an IP address. */
typedef struct {
    uint8_t mac_address[6];     /* MAC address */
    uint32_t ip_address;        /* Leased IP address */
    uint32_t expire_time;       /* When the lease expire (absolute)*/
}__attribute__((packed)) dhcp_hash_elem;

/* HASHMAP-mode lease pool */
typedef struct {
    struct hash_tab_mac leases;
    struct hash_tab_ip ip_set;  
    uint32_t next_ip;
    uint16_t max_leases;
} dhcp_hashpool_t;

void dhcp_hashpool_init(dhcp_hashpool_t *pool, uint32_t pool_start, uint16_t max_leases);
uint32_t dhcp_hashpool_find_available_ip(dhcp_hashpool_t *pool, uint32_t pool_start, uint32_t pool_end, uint8_t *mac, uint32_t cur_time);
dhcp_hash_elem *dhcp_hashpool_find_lease(dhcp_hashpool_t *pool, uint8_t *mac);
bool dhcp_hashpool_alloc_lease(dhcp_hashpool_t *pool, uint32_t ip, uint8_t *mac, uint32_t lease_time, uint32_t cur_time);
void dhcp_hashpool_cleanup_expire_lease(dhcp_hashpool_t *pool, uint32_t cur_time); 

#endif /* DHCP_HASHMAP_H */
