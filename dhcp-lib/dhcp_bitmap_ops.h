#ifndef DHCP_BITMAP_OPS_H
#define DHCP_BITMAP_OPS_H

/*
 * Shared bitmap constants and bit-manipulation helpers used by 
 * all bitmap-based IP pool headers.
 */

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t *ips;
    uint32_t expire_time;
}  dhcp_bmrange_t;

/*
 * Bitmap operations.
 * All operate on a uint32_t array where bit i reprsesents IP slot i within a range.
 */
void dhcp_bm_set(uint32_t *ips, uint32_t bit);                              // mark slot as in-use
void dhcp_bm_clear(uint32_t *ips, uint32_t bit);                            // mark slot as free
bool dhcp_bm_used(const uint32_t *ips, uint32_t bit, uint32_t range_size);  // return slot status

uint32_t dhcp_bm_counter_to_ip(uint32_t range_start, uint32_t counter);
bool dhcp_bm_range_full(uint32_t counter, uint32_t range_size);

#endif /* DHCP_BITMAP_OPS_H */
