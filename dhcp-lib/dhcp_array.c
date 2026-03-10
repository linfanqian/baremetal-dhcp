#include "dhcp_array.h"
#include "dhcp_compat.h"

void dhcp_arraypool_init(dhcp_arraypool_t *pool, uint32_t pool_start,
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
uint32_t dhcp_arraypool_find_available_ip(dhcp_arraypool_t *pool,
                                          uint32_t pool_start, uint32_t pool_end,
                                          uint8_t *mac, uint32_t cur_time) {
    /* Check for clean up first*/
    if (pool->lease_count >= pool->max_leases)
        dhcp_arraypool_cleanup_expire_lease(pool, cur_time); 
        
    /* Check if MAC already has a lease */
    dhcp_lease_t *existing = dhcp_arraypool_find_lease(pool, mac);
    if (existing)
        return existing->ip_address;
    
    /* Pool full — don't offer anything */
    if (pool->lease_count >= pool->max_leases)
        return 0;

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
dhcp_lease_t *dhcp_arraypool_find_lease(dhcp_arraypool_t *pool, uint8_t *mac) {
    for (uint16_t i = 0; i < pool->max_leases; i++) {
        if (pool->leases[i].in_use &&
            kmemcmp(pool->leases[i].mac_address, mac, 6) == 0) {
            return &pool->leases[i];
        }
    }
    return (dhcp_lease_t *)0;
}


/* Allocate a new lease */
bool dhcp_arraypool_alloc_lease(dhcp_arraypool_t *pool, uint32_t ip, uint8_t *mac,
                                uint32_t lease_time, uint32_t cur_time) {
    /* Check for clean up first*/
    if (pool->lease_count >= pool->max_leases)
        dhcp_arraypool_cleanup_expire_lease(pool, cur_time); 
        
    dhcp_lease_t *mac_match = (dhcp_lease_t *)0;  /* existing lease for this MAC */
    dhcp_lease_t *ip_match  = (dhcp_lease_t *)0;  /* existing lease holding this IP */
    dhcp_lease_t *free_slot = (dhcp_lease_t *)0;  /* first free slot found */

    for (uint16_t i = 0; i < pool->max_leases; i++) {
        if (!pool->leases[i].in_use) {
            if (!free_slot) free_slot = &pool->leases[i];
            continue;
        }
        if (kmemcmp(pool->leases[i].mac_address, mac, 6) == 0)
            mac_match = &pool->leases[i];
        if (pool->leases[i].ip_address == ip)
            ip_match = &pool->leases[i];

        if ((mac_match || ip_match) && free_slot) break;
    }

    if (mac_match) {
        /* MAC already has a lease */
        if (mac_match->ip_address != ip)
            return false;          /* requesting a different IP — NAK */
        else {
            mac_match->expire_time = cur_time + lease_time;
            return true;           /* renewal */
        }
    }

    if (ip_match)
        return false;              /* IP already taken by a different MAC — NAK */

    if (!free_slot)
        return false;              /* pool full */

    free_slot->ip_address  = ip;
    kmemcpy(free_slot->mac_address, mac, 6);
    free_slot->expire_time = cur_time + lease_time;
    free_slot->in_use      = 1;
    pool->lease_count++;
    return true;
}

void dhcp_arraypool_cleanup_expire_lease(dhcp_arraypool_t *pool, uint32_t cur_time) {
    for (uint16_t i = 0; i < pool->max_leases; i++) {
        if (pool->leases[i].in_use && pool->leases[i].expire_time <= cur_time) {
            pool->leases[i].in_use = 0;
            pool->lease_count--;
        }
    }
    return; 
}

void dhcp_arraypool_decline_lease(dhcp_arraypool_t *pool, uint8_t *mac) {
    dhcp_lease_t *lease = dhcp_arraypool_find_lease(pool, mac);
    if (lease) {
        lease->in_use = 0;
        pool->lease_count--;
    }
}

