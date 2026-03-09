#ifndef DHCP_BITMAP_VARTIME_H
#define DHCP_BITMAP_VARTIME_H

/*
 * Bitmap IP pool that only one range is needed. The range runs for a fixed 
 * length of time and then reset. The lease time of each offered IP is the 
 * remaining time of the range.
 * 
 * If the range is full, we reset it only if a new DISCOVER comes in.
 *
 * No MAC→IP mapping and no per-IP timestamp are stored.
 */

#include "dhcp_bitmap_ops.h"

/* IP pool */
typedef struct {
    uint32_t pool_start;        /* First IP of the overall IP pool */
    uint32_t lease_time;        /* Lease duration in seconds */
    dhcp_bm_offcnt_t counter;   /* Monotonically increasing offer counter */
    dhcp_bmrange_t *range;      /* The single range of IP */
    uint32_t range_size;
} dhcp_bmpool_var_t;

void dhcp_bmpool_var_recycle(dhcp_bmpool_var_t *pool);
void dhcp_bmpool_var_init(dhcp_bmpool_var_t *pool, uint32_t pool_start, uint32_t lease_time,
                          dhcp_bmrange_t *range, uint32_t range_size);

/* Peek at the next available IP without committing it, providing the actual lease time */
uint32_t dhcp_bmpool_var_peek(dhcp_bmpool_var_t *pool, uint32_t cur_time, uint32_t *lease_time);
/* Commit a specific IP address, return true if successful */
bool dhcp_bmpool_var_commit_ip(dhcp_bmpool_var_t *pool, uint32_t ip);

#endif /* DHCP_BITMAP_VARTIME_H */
