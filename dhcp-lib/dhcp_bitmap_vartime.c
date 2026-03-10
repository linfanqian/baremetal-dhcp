#include "dhcp_bitmap_vartime.h"
#include "dhcp_compat.h"

void dhcp_bmpool_var_recycle(dhcp_bmpool_var_t *pool) {
    kmemset(pool->range->ips, 0, ((pool->range_size + 31u) / 32u) * sizeof(uint32_t));
    pool->range->expire_time = 0;
    pool->counter = 0;
}

void dhcp_bmpool_var_init(dhcp_bmpool_var_t *pool, uint32_t pool_start, uint32_t lease_time,
                          dhcp_bmrange_t *range, uint32_t range_size) {
    pool->pool_start = pool_start;
    pool->lease_time = lease_time;
    pool->range = range;
    pool->range_size = range_size;
    dhcp_bmpool_var_recycle(pool);
}

uint32_t dhcp_bmpool_var_peek(dhcp_bmpool_var_t *pool, uint32_t cur_time) {
    // Recycle if the range window has expired
    if (pool->range->expire_time > 0 && pool->range->expire_time <= cur_time) {
        dhcp_bmpool_var_recycle(pool);
    }

    // If the counter has exhausted all slots, range is full — wait for expiry
    uint32_t cur_ip = dhcp_bm_counter_to_ip(pool->pool_start, pool->counter);
    if (pool->range->expire_time > 0 && dhcp_bm_range_full(pool->pool_start, cur_ip, pool->range_size)) 
        return 0u;

    // Start the range timer on the first offer
    if (pool->range->expire_time == 0) {
        pool->range->expire_time = cur_time + pool->lease_time;
    }

    // From current point, traverse the pool until finding an available IP
    uint32_t candidate = 0;
    while (pool->counter < pool->range_size) {
        candidate = dhcp_bm_counter_to_ip(pool->pool_start, pool->counter);
        pool->counter++;
        if (!dhcp_bm_used(pool->range->ips, candidate - pool->pool_start, pool->range_size))
            break;
    }
    
    if (candidate == 0 ||
        dhcp_bm_used(pool->range->ips, candidate - pool->pool_start, pool->range_size)) 
        return 0u;
    
    return candidate;
}

bool dhcp_bmpool_var_commit_ip(dhcp_bmpool_var_t *pool, uint32_t ip) {
    if (ip < pool->pool_start || ip >= pool->pool_start + pool->range_size) return false;
    uint32_t bit = ip - pool->pool_start;
    if (dhcp_bm_used(pool->range->ips, bit, pool->range_size)) return false;
    dhcp_bm_set(pool->range->ips, bit);
    return true;
}
