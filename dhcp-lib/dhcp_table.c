#include "dhcp_table.h"
#include "dhcp_compat.h"

void dhcp_tablepool_init(dhcp_tablepool_t *pool, uint32_t pool_start,
                         dhcp_lease_t *leases, uint16_t max_leases) {
    pool->leases = leases;
    pool->max_leases = max_leases;
    pool->lease_count = 0;
    pool->next_ip = pool_start;
    if (leases) {
        for (uint16_t i = 0; i < max_leases; i++)
            pool->leases[i].in_use = 0;
    }
}

/* Find an available IP address from the pool */
uint32_t dhcp_tablepool_find_available_ip(dhcp_tablepool_t *pool,
                                          uint32_t pool_start, uint32_t pool_end,
                                          uint8_t *mac, uint32_t cur_time) {
    /* Check if MAC already has a lease */
    dhcp_lease_t *existing = dhcp_tablepool_find_lease(pool, mac);
    if (existing)
        return existing->ip_address;

    uint32_t range_size = pool_end - pool_start + 1;
    for (uint32_t count = 0; count < range_size; count++) {
        uint32_t ip = pool->next_ip;
        pool->next_ip = pool_start + ((ip - pool_start + 1) % range_size);

        uint8_t found = 0;
        for (uint16_t i = 0; i < pool->max_leases; i++) {
            if (pool->leases[i].in_use && pool->leases[i].ip_address == ip) {
                found = 1;
                break;
            }
        }
        if (!found)
            return ip;   
    }
    return 0; /* No available IP */
}

/* Find a lease by MAC address */
dhcp_lease_t *dhcp_tablepool_find_lease(dhcp_tablepool_t *pool, uint8_t *mac) {
    for (uint16_t i = 0; i < pool->max_leases; i++) {
        if (pool->leases[i].in_use &&
            kmemcmp(pool->leases[i].mac_address, mac, 6) == 0) {
            return &pool->leases[i];
        }
    }
    return (dhcp_lease_t *)0;
}

/* Allocate a new lease */
bool dhcp_tablepool_alloc_lease(dhcp_tablepool_t *pool, uint32_t ip, uint8_t *mac,
                                uint32_t lease_time, uint32_t cur_time) {
    // check if a lease exists with this MAC
    dhcp_lease_t *existing = dhcp_tablepool_find_lease(pool, mac);
    if (existing) {
        if (existing->ip_address != ip) {
            return false;
        }
        else {
            existing->expire_time = cur_time + lease_time;
            return true;
        }
    } 

    if (pool->lease_count >= pool->max_leases)
        return false;

    /* find a reusable freed slot first */
    dhcp_lease_t *lease = (dhcp_lease_t *)0;
    for (uint16_t i = 0; i < pool->max_leases; i++) {
        if (!pool->leases[i].in_use) { lease = &pool->leases[i]; break; }
    }

    lease->ip_address = ip;
    kmemcpy(lease->mac_address, mac, 6);
    lease->expire_time = cur_time + lease_time;
    lease->in_use = 1;

    pool->lease_count++;
    return true;
}

void dhcp_tablepool_cleanup_expire_lease(dhcp_tablepool_t *pool, uint32_t cur_time) {
    for (uint16_t i = 0; i < pool->max_leases; i++) {
        if (pool->leases[i].in_use && pool->leases[i].expire_time <= cur_time) {
            pool->leases[i].in_use = 0;
            pool->lease_count--;
        }
    }
    return; 
}

