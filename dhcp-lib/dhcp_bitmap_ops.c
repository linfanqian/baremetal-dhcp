#include "dhcp_bitmap_ops.h"

void dhcp_bm_udivmod(uint64_t n, uint64_t d, uint64_t *quot, uint64_t *rem) {
    uint64_t q = 0u, r = 0u;
    for (int i = 63; i >= 0; --i) {
        r = (r << 1u) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= (1u << i); }
    }
    *quot = q;
    *rem  = r;
}

void dhcp_bm_set(dhcp_bm_range_t *range, uint32_t bit) {
    range->ips[bit >> 5u] |= (1u << (bit & 31u));
}

void dhcp_bm_clear(dhcp_bm_range_t *range, uint32_t bit) {
    range->ips[bit >> 5u] &= ~(1u << (bit & 31u));
}

bool dhcp_bm_used(const dhcp_bm_range_t *range, uint32_t bit) {
    if (bit >= DHCP_BITMAP_RANGE_SIZE) return true;
    return (bool)((range->ips[bit >> 5u] >> (bit & 31u)) & 1u);
}

bool dhcp_bm_range_full(uint32_t range_start, uint32_t cur_ip) {
    if (cur_ip < range_start) return false;
    return (cur_ip - range_start == DHCP_BITMAP_RANGE_SIZE - 1);
}

void dhcp_bm_offcnt_init(dhcp_bm_offcnt_t *c) {
    c->count = 0;
}

uint32_t dhcp_bm_next_ip(dhcp_bm_offcnt_t *c, uint32_t range_start) {
    uint64_t quot, rem;
    dhcp_bm_udivmod(c->count, DHCP_BITMAP_RANGE_SIZE, &quot, &rem);
    uint32_t ip = range_start + (uint32_t)rem;
    c->count++;
    return ip;
}

uint32_t dhcp_bm_last_ip(dhcp_bm_offcnt_t *c, uint32_t range_start) {
    if (c->count == 0) return 0;
    uint64_t quot, rem;
    dhcp_bm_udivmod(c->count - 1, DHCP_BITMAP_RANGE_SIZE, &quot, &rem);
    return range_start + (uint32_t)rem;
}
