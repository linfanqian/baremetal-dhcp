#include "dhcp_bitmap_unitime.h"
#include "dhcp_compat.h"

static uint32_t range_base(const dhcp_bmpool_uni_t *pool) {
    return pool->pool_start + (uint32_t)pool->cur_range * pool->range_size;
}

static bool dhcp_bmpool_uni_ip_used(const dhcp_bmpool_uni_t *pool, uint32_t ip) {
    uint32_t rbase = range_base(pool);
    if (ip < rbase || ip >= rbase + pool->range_size) return true;
    uint32_t bit = ip - rbase;
    return dhcp_bm_used(pool->ranges[pool->cur_range].ips, bit, pool->range_size);
}

void dhcp_bmpool_uni_recycle(dhcp_bmpool_uni_t *pool, uint32_t range_idx) {
    if (range_idx >= pool->num_ranges) return;
    kmemset(pool->ranges[range_idx].ips, 0, ((pool->range_size + 31u) / 32u) * sizeof(uint32_t));
    pool->ranges[range_idx].expire_time = 0;
}

void dhcp_bmpool_uni_init(dhcp_bmpool_uni_t *pool, uint32_t pool_start, uint32_t lease_time,
                          dhcp_bmrange_t *ranges, uint32_t range_size, uint8_t num_ranges) {
    pool->pool_start = pool_start;
    pool->lease_time = lease_time;
    pool->ranges = ranges;
    pool->range_size = range_size;
    pool->num_ranges = num_ranges;
    pool->cur_range = 0;
    for (uint8_t i = 0; i < num_ranges; ++i) {
        dhcp_bmpool_uni_recycle(pool, i);
    }
}

uint32_t dhcp_bmpool_uni_peek(dhcp_bmpool_uni_t *pool, uint32_t cur_time) {
    // Try to advance to next range if current range is full
    if (dhcp_bm_range_full(pool->counter, pool->range_size)) {
        uint8_t next_range = (uint8_t)((pool->cur_range + 1u) % pool->num_ranges);
        if (pool->ranges[next_range].expire_time > 0 &&
            pool->ranges[next_range].expire_time <= cur_time) {
            // Recycle next range
            dhcp_bmpool_uni_recycle(pool, next_range);
        }

        if (pool->ranges[next_range].expire_time == 0) {
            pool->cur_range = next_range;
            pool->counter = 0;
        } else {
            // No range is available
            return 0u;
        }
    }

    // We are now on an available range
    // From current point, traverse the pool until finding an available IP
    uint32_t rbase = range_base(pool);
    uint32_t candidate = 0;
    while (pool->counter < pool->range_size) {
        candidate = dhcp_bm_counter_to_ip(rbase, pool->counter);
        pool->counter++;
        if (!dhcp_bm_used(pool->ranges[pool->cur_range].ips, candidate - rbase, pool->range_size))
            break;
    }
    
    if (candidate == 0 || dhcp_bmpool_uni_ip_used(pool, candidate)) return 0u;
    
    // Set expiration time if current range is full
    // The expiration time is set here in case REQUEST is never received
    if (pool->ranges[pool->cur_range].expire_time == 0 && 
        dhcp_bm_range_full(pool->counter, pool->range_size)) {
        pool->ranges[pool->cur_range].expire_time = cur_time + pool->lease_time;
    }

    return candidate;
}

bool dhcp_bmpool_uni_commit_ip(dhcp_bmpool_uni_t *pool, uint32_t ip) {
    if (dhcp_bmpool_uni_ip_used(pool, ip)) return false;

    uint32_t bit = ip - range_base(pool);
    dhcp_bm_set(pool->ranges[pool->cur_range].ips, bit);
    return true;
}
