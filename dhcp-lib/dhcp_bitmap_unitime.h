#ifndef DHCP_BITMAP_UNITIME_H
#define DHCP_BITMAP_UNITIME_H

/*
 * Bitmap IP pool where every lease in a range carries the same (full) 
 * lease duration, and the expiration timer starts only when the range is
 * full.
 *
 * No MAC→IP mapping and no per-IP timestamp are stored.
 * 
 * This method requires at least 2 IP ranges
 */

#include "dhcp_bitmap_ops.h"

/* IP pool of multiple ranges */
typedef struct {
    uint32_t pool_start;        /* First IP of the overall IP pool */
    uint32_t lease_time;        /* Lease duration in seconds */
    dhcp_bm_offcnt_t counter;   /* Monotonically increasing offer counter */

    dhcp_bm_range_t ranges[DHCP_BITMAP_MAX_RANGES];
    uint8_t cur_range;          /* Current range index */
} dhcp_bmpool_uni_t;

void dhcp_bmpool_uni_recycle(dhcp_bmpool_uni_t *pool, uint32_t range_idx);
void dhcp_bmpool_uni_init(dhcp_bmpool_uni_t *pool, uint32_t pool_start, uint32_t lease_time);

/* Peek at the next available IP without committing it */
uint32_t dhcp_bmpool_uni_peek(dhcp_bmpool_uni_t *pool, uint32_t cur_time);
/* Commit a specific IP address, return true if successful */
bool dhcp_bmpool_uni_alloc_ip(dhcp_bmpool_uni_t *pool, uint32_t ip);

#endif /* DHCP_BITMAP_UNITIME_H */
