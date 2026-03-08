#ifndef __DHCP_NPRC_H__
#define __DHCP_NPRC_H__


#define BITMAP_SIZE     (sizeof(unsigned long) * 8)
#define BITMAP_FLUSH    BITMAP_SIZE / 4

typedef struct dhcp_nprcpool {
  unsigned int cache_base;
  unsigned char offer_next;  
  unsigned long ack_bitmap;
} dhcp_nprcpool_t;

int dhcp_nprc_find_available_ip(dhcp_nprcpool_t *p, unsigned int pool_start,
        unsigned int pool_end);
int dhcp_nprc_commit_ip(dhcp_nprcpool_t *p, unsigned int requested_ip,
        unsigned int pool_start, unsigned int pool_end);

#endif
