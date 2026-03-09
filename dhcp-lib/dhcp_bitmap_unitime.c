#include "dhcp_bitmap_unitime.h"
#include "dhcp_compat.h"

static uint32_t pool_size(const dhcp_bmpool_uni_t *pool) {
    return pool->range_size * pool->num_ranges;
}

static uint32_t range_base(const dhcp_bmpool_uni_t *pool, uint8_t range_idx) {
    return pool->pool_start + (uint32_t)range_idx * pool->range_size;
}

static bool dhcp_bmpool_uni_ip_used(const dhcp_bmpool_uni_t *pool, uint32_t ip) {
    if (ip < pool->pool_start || ip >= pool->pool_start + pool_size(pool)) return 0u;
    uint32_t bit = ip - range_base(pool, pool->cur_range);
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
    dhcp_bm_offcnt_init(&pool->counter);
    for (uint8_t i = 0; i < num_ranges; ++i) {
        dhcp_bmpool_uni_recycle(pool, i);
    }
}

uint32_t dhcp_bmpool_uni_peek(dhcp_bmpool_uni_t *pool, uint32_t cur_time) {
    uint32_t range_start = range_base(pool, pool->cur_range);

    // Try to advance to next range if current range is full
    uint32_t last_ip = dhcp_bm_last_ip(&(pool->counter), range_start, pool->range_size);
    if (dhcp_bm_range_full(range_start, last_ip, pool->range_size)) {
        uint8_t next_range = (uint8_t)((pool->cur_range + 1u) % pool->num_ranges);
        if (pool->ranges[next_range].expire_time > 0 &&
            pool->ranges[next_range].expire_time <= cur_time) {
            // Recycle next range
            dhcp_bmpool_uni_recycle(pool, next_range);
        }

        if (pool->ranges[next_range].expire_time == 0) {
            pool->cur_range = next_range;
            range_start = range_base(pool, pool->cur_range);
        } else {
            // No range is available
            return 0u;
        }
    }

    // We are now on an available range
    uint32_t candidate = dhcp_bm_next_ip(&pool->counter, range_start, pool->range_size);
    if (dhcp_bmpool_uni_ip_used(pool, candidate)) return 0u;
    
    // Set expiration time + 5s grace period if current range is full
    // The expiration time is set here in case REQUEST is never received
    if (dhcp_bm_range_full(range_start, candidate, pool->range_size)) {
        pool->ranges[pool->cur_range].expire_time = cur_time + pool->lease_time + 5;
    }

    return candidate;
}

bool dhcp_bmpool_uni_commit_ip(dhcp_bmpool_uni_t *pool, uint32_t ip) {
    if (ip < pool->pool_start || ip >= pool->pool_start + pool_size(pool)) return false;
    if (dhcp_bmpool_uni_ip_used(pool, ip)) return false;

    uint32_t offset = ip - pool->pool_start;
    uint8_t range_idx = (uint8_t)(offset / pool->range_size);
    uint32_t bit = offset % pool->range_size;
    dhcp_bm_set(pool->ranges[range_idx].ips, bit);
    return true;
}
