#include "dhcp_bitmap_unitime.h"
#include "dhcp_compat.h"

#define POOL_SIZE (DHCP_BITMAP_MAX_RANGES * DHCP_BITMAP_RANGE_SIZE)

static uint32_t range_base(const dhcp_bmpool_uni_t *pool, uint8_t range_idx) {
    return pool->pool_start + (uint32_t)range_idx * DHCP_BITMAP_RANGE_SIZE;
}

static bool dhcp_bmpool_uni_ip_used(const dhcp_bmpool_uni_t *pool, uint32_t ip) {
    if (ip < pool->pool_start || ip >= pool->pool_start + POOL_SIZE) return 0u;
    uint32_t bit = ip - range_base(pool, pool->cur_range);
    return dhcp_bm_used(&(pool->ranges[pool->cur_range]), bit);
}

void dhcp_bmpool_uni_recycle(dhcp_bmpool_uni_t *pool, uint32_t range_idx) {
    if (range_idx >= DHCP_BITMAP_MAX_RANGES) return;
    kmemset(pool->ranges[range_idx].ips, 0, sizeof(pool->ranges[range_idx].ips));
    pool->ranges[range_idx].expire_time = 0;
}

void dhcp_bmpool_uni_init(dhcp_bmpool_uni_t *pool, uint32_t pool_start, uint32_t lease_time) {
    pool->pool_start = pool_start;
    pool->lease_time = lease_time;
    pool->cur_range = 0;
    dhcp_bm_offcnt_init(&pool->counter);
    for (uint8_t i = 0; i < DHCP_BITMAP_MAX_RANGES; ++i) {
        dhcp_bmpool_uni_recycle(pool, i);
    }
}

uint32_t dhcp_bmpool_uni_peek(dhcp_bmpool_uni_t *pool, uint32_t cur_time) {
    uint32_t range_start = range_base(pool, pool->cur_range);

    // Try to advance to next range if current range is full
    uint32_t last_ip = dhcp_bm_last_ip(&(pool->counter), range_start);
    if (dhcp_bm_range_full(range_start, last_ip)) {
        uint8_t next_range = (uint8_t)((pool->cur_range + 1u) % DHCP_BITMAP_MAX_RANGES);
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
    uint32_t candidate = dhcp_bm_next_ip(&pool->counter, range_start);
    if (dhcp_bmpool_uni_ip_used(pool, candidate)) return 0u;
    
    // Set expiration time + 5s grace period if current range is full
    // The expiration time is set here in case REQUEST is never received
    if (dhcp_bm_range_full(range_start, candidate)) {
        pool->ranges[pool->cur_range].expire_time = cur_time + pool->lease_time + 5;
    }

    return candidate;
}

bool dhcp_bmpool_uni_commit_ip(dhcp_bmpool_uni_t *pool, uint32_t ip) {
    if (ip < pool->pool_start || ip >= pool->pool_start + POOL_SIZE) return false;
    if (dhcp_bmpool_uni_ip_used(pool, ip)) return false;

    uint32_t offset = ip - pool->pool_start;
    uint8_t range_idx = (uint8_t)(offset / DHCP_BITMAP_RANGE_SIZE);
    uint32_t bit = offset % DHCP_BITMAP_RANGE_SIZE;
    dhcp_bm_set(&(pool->ranges[range_idx]), bit);
    return true;
}
