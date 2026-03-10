#include "dhcp_bitmap_ops.h"


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

uint32_t dhcp_bm_counter_to_ip(uint32_t range_start, uint32_t counter) {
    return range_start + counter;
}

bool dhcp_bm_range_full(uint32_t counter, uint32_t range_size) {
    return (counter >= range_size);
}
