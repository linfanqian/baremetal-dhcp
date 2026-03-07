#ifndef DHCP_BITMAP_OPS_H
#define DHCP_BITMAP_OPS_H

/*
 * Shared bitmap constants and bit-manipulation helpers used by 
 * all bitmap-based IP pool headers.
 */

#include <stdint.h>
#include <stdbool.h>

/*
 * Tuneable compile-time parameters
 * Override with -DDHCP_BITMAP_MAX_RANGE_SIZE=N and -DDHCP_BITMAP_MAX_RANGES=M
 */

// Maximum number of range slices per pool.
#ifndef DHCP_BITMAP_MAX_RANGES
#define DHCP_BITMAP_MAX_RANGES 2u
#endif

// Maximum IPs per range slice. 
#ifndef DHCP_BITMAP_RANGE_SIZE
#define DHCP_BITMAP_RANGE_SIZE 1024u
#endif

// 32-bit words required to hold one range's full bitmap.
#define DHCP_BITMAP_WORDS_PER_RANGE \
    ((DHCP_BITMAP_RANGE_SIZE + 31u) / 32u)

// One IP range tracked by a bitmap
typedef struct {
    uint32_t ips[DHCP_BITMAP_WORDS_PER_RANGE];
    uint32_t expire_time;
}  dhcp_bm_range_t;

void dhcp_bm_udivmod(uint64_t n, uint64_t d, uint64_t *quot, uint64_t *rem);    // div+mod

/*
 * Bitmap operations.
 * All operate on a uint32_t array where bit i reprsesents IP slot i within a range.
 */
void dhcp_bm_set(dhcp_bm_range_t *range, uint32_t bit);           // mark slot as in-use
void dhcp_bm_clear(dhcp_bm_range_t *range, uint32_t bit);         // mark slot as free
bool dhcp_bm_used(const dhcp_bm_range_t *range, uint32_t bit);    // return slot status

bool dhcp_bm_range_full(uint32_t range_start, uint32_t cur_ip);

/*
 * Monotonic offer counter: assign each DISCOVER a unique counter value.
 * This value is mapped to an IP in the pool by range_start + (count % range_size)
 */
typedef struct { uint64_t count; } dhcp_bm_offcnt_t;
void dhcp_bm_offcnt_init(dhcp_bm_offcnt_t *c);
uint32_t dhcp_bm_next_ip(dhcp_bm_offcnt_t *c, uint32_t range_start);
uint32_t dhcp_bm_last_ip(dhcp_bm_offcnt_t *c, uint32_t range_start); // return 0 if no last IP

#endif /* DHCP_BITMAP_OPS_H */
