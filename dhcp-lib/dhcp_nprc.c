#include "dhcp_nprc.h"

int dhcp_nprc_find_available_ip(dhcp_nprcpool_t *p, unsigned int pool_start,
        unsigned int pool_end) {
    if (p->offer_next >= BITMAP_SIZE) {
        p->ack_bitmap >>= BITMAP_FLUSH; 
        p->cache_base += BITMAP_FLUSH;
        p->offer_next -= BITMAP_FLUSH;
    }
            
    unsigned char offset = p->offer_next;
    unsigned int offered_ip = pool_start + p->cache_base + offset;
    if (offered_ip > pool_end) 
        return 0;
   
    p->offer_next++;
    return offered_ip;
}

int dhcp_nprc_commit_ip(dhcp_nprcpool_t *p, unsigned int requested_ip,
        unsigned int pool_start, unsigned int pool_end) {
    unsigned int ip_lbound = pool_start + p->cache_base;
    unsigned int ip_ubound = (pool_start + p->cache_base + 
            p->offer_next - 1);

    // just in case
    if (ip_ubound > pool_end) ip_ubound = pool_end;

    if (requested_ip >= ip_lbound && requested_ip <= ip_ubound) {
        unsigned int offset = requested_ip - pool_start - p->cache_base;  
        if ((offset < BITMAP_SIZE) && 
                (!(p->ack_bitmap & (1UL << offset)))) {

            p->ack_bitmap |= (1UL << offset);
            // clean as many spots as possible 
            while ((p->ack_bitmap & 1UL) && p->offer_next) {
                p->ack_bitmap >>= 1; 
                p->cache_base++;
                p->offer_next--;
            }
            return 1;
        }
    }
    return 0;
}
