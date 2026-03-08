#include "dhcp_bitmap_vartime.h"
#include "dhcp_compat.h"

void dhcp_bmpool_var_recycle(dhcp_bmpool_var_t *pool) {
    kmemset(pool->range.ips, 0, sizeof(pool->range.ips));
    pool->range.expire_time = 0;
}

void dhcp_bmpool_var_init(dhcp_bmpool_var_t *pool, uint32_t pool_start, uint32_t lease_time) {
    pool->pool_start = pool_start;
    pool->lease_time = lease_time;
    dhcp_bm_offcnt_init(&pool->counter);
    dhcp_bmpool_var_recycle(pool);
}

uint32_t dhcp_bmpool_var_peek(dhcp_bmpool_var_t *pool, uint32_t cur_time, uint32_t *lease_time) {
    // Recycle if the range window has expired
    if (pool->range.expire_time > 0 && pool->range.expire_time <= cur_time) {
        dhcp_bmpool_var_recycle(pool);
    }

    // If the counter has exhausted all slots, range is full — wait for expiry
    uint32_t last_ip = dhcp_bm_last_ip(&(pool->counter), pool->pool_start);
    if (pool->range.expire_time > 0 && dhcp_bm_range_full(pool->pool_start, last_ip)) 
        return 0u;

    // Start the range timer on the first offer
    if (pool->range.expire_time == 0) {
        pool->range.expire_time = cur_time + pool->lease_time;
    }

    uint32_t candidate = dhcp_bm_next_ip(&pool->counter, pool->pool_start);
    if (dhcp_bm_used(&pool->range, candidate - pool->pool_start)) return 0u;
    *lease_time = pool->range.expire_time - cur_time;

    return candidate;
}

bool dhcp_bmpool_var_commit_ip(dhcp_bmpool_var_t *pool, uint32_t ip) {
    if (ip < pool->pool_start || ip >= pool->pool_start + DHCP_BITMAP_RANGE_SIZE) return false;
    uint32_t bit = ip - pool->pool_start;
    if (dhcp_bm_used(&pool->range, bit)) return false;
    dhcp_bm_set(&pool->range, bit);
    return true;
}
