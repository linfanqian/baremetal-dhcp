#ifndef DHCP_TABLE_H
#define DHCP_TABLE_H

/*
 * TABLE-mode IP lease pool.
 * Tracks per-client leases in a caller-provided array, keyed by MAC address.
 */

#include <stdint.h>
#include <stdbool.h>

/* One lease entry, mapping a client MAC to an IP address. */
typedef struct {
    uint32_t ip_address;        /* Leased IP address */
    uint8_t mac_address[6];     /* MAC address */
    uint32_t expire_time;       /* When the lease expire (absolute)*/
    uint8_t in_use;             /* Whether this lease is active */
} dhcp_lease_t;

/* TABLE-mode lease pool */
typedef struct {
    dhcp_lease_t *leases;
    uint16_t max_leases;
    uint16_t lease_count;
    uint32_t next_ip;
} dhcp_tablepool_t;

void dhcp_tablepool_init(dhcp_tablepool_t *pool, uint32_t pool_start, dhcp_lease_t *leases, uint16_t max_leases);
uint32_t dhcp_tablepool_find_available_ip(dhcp_tablepool_t *pool, uint32_t pool_start, uint32_t pool_end, uint8_t *mac, uint32_t cur_time);
dhcp_lease_t *dhcp_tablepool_find_lease(dhcp_tablepool_t *pool, uint8_t *mac);
bool dhcp_tablepool_alloc_lease(dhcp_tablepool_t *pool, uint32_t ip, uint8_t *mac, uint32_t lease_time, uint32_t cur_time);
void dhcp_tablepool_cleanup_expire_lease(dhcp_tablepool_t *pool, uint32_t cur_time); 

#endif /* DHCP_TABLE_H */
