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

void dhcp_bm_set(uint32_t *ips, uint32_t bit) {
    ips[bit >> 5u] |= (1u << (bit & 31u));
}

void dhcp_bm_clear(uint32_t *ips, uint32_t bit) {
    ips[bit >> 5u] &= ~(1u << (bit & 31u));
}

bool dhcp_bm_used(const uint32_t *ips, uint32_t bit, uint32_t range_size) {
    if (bit >= range_size) return true;
    return (bool)((ips[bit >> 5u] >> (bit & 31u)) & 1u);
}

bool dhcp_bm_range_full(uint32_t range_start, uint32_t cur_ip, uint32_t range_size) {
    if (cur_ip < range_start) return false;
    return (cur_ip - range_start == range_size - 1);
}

void dhcp_bm_offcnt_init(dhcp_bm_offcnt_t *c) {
    c->count = 0;
}

uint32_t dhcp_bm_next_ip(dhcp_bm_offcnt_t *c, uint32_t range_start, uint32_t range_size) {
    uint64_t quot, rem;
    dhcp_bm_udivmod(c->count, range_size, &quot, &rem);
    uint32_t ip = range_start + (uint32_t)rem;
    c->count++;
    return ip;
}

uint32_t dhcp_bm_last_ip(dhcp_bm_offcnt_t *c, uint32_t range_start, uint32_t range_size) {
    if (c->count == 0) return 0;
    uint64_t quot, rem;
    dhcp_bm_udivmod(c->count - 1, range_size, &quot, &rem);
    return range_start + (uint32_t)rem;
}
