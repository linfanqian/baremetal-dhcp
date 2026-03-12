#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#include "headers.h"
#include "frame.h"

static uint16_t ip_cksum(void* data, int len) {
    uint16_t *p = data;
    uint32_t cksum = 0;
    for (; len > 1; len -= 2) cksum += *p++;
    if (len) cksum += *(uint8_t *)p;
    while (cksum >> 16) cksum = (cksum & 0xffff) + (cksum >> 16);
    return ~cksum;
}

int build_frame(unsigned char *buf, int buflen, const struct build_meta meta) {
    
    if (buflen < MIN_SEND_LEN) 
        return 0;

    memset(buf, 0x0, buflen);

    struct EthernetHdr *eth = (struct EthernetHdr *)buf;
    struct IPv4Hdr *ip = (struct IPv4Hdr *)(buf + ETHER_LEN);
    struct UDPHdr *udp = (struct UDPHdr *)(buf + ETHER_LEN + IPv4_LEN);
    struct DHCP *dhcp = (struct DHCP *)(buf + ETHER_LEN + IPv4_LEN + UDP_LEN);

    // ethernet
    memset(eth->dstMAC, 0xff, 6); // broadcast
    memcpy(eth->srcMAC, meta.client_mac, 6);
    eth->etherType = htons(0x0800);

    // ip
    ip->versionIhl = 0x45;
    ip->ttl = 64;
    ip->protocol = 17; // udp
    memset(ip->srcIP, 0x00, 4);
    memset(ip->dstIP, 0xff, 4);
    int udp_len = UDP_LEN + DHCP_LEN;
    int ip_len = IPv4_LEN + udp_len;
    ip->totalLen = htons(ip_len);
    ip->flags = htons(0x4000);  // no fragment
    ip->checksum = 0;
    ip->checksum = ip_cksum(ip, IPv4_LEN);

    // udp
    udp->srcPort = htons(68);
    udp->dstPort = htons(67);
    udp->totalLen = htons(udp_len);
    udp->checksum = 0;

    // dhcp
    dhcp->hdr.op = 1; // boot request
    dhcp->hdr.htype = 1;    // ethernet
    dhcp->hdr.hlen = 6;
    dhcp->hdr.xid = htonl(meta.xid);
    dhcp->hdr.flags = htons(0x8000);    // request broadcast reply
    dhcp->hdr.magic = htonl(0x63825363);
    memcpy(dhcp->hdr.chaddr, meta.client_mac, 6);

    uint8_t *opt = dhcp->options;
    *opt++ = OPT_MSG_TYPE; 
    *opt++ = 1;
    *opt++ = meta.dhcp_type; // dhcp discover flag
    // TODO: add other type specifications
    if (meta.dhcp_type == DHCP_REQUEST) {
        *opt++ = OPT_REQUEST_IP;
        *opt++ = 4;
        memcpy(opt, meta.requested_ip, 4);
        opt += 4;

        *opt++ = OPT_SERVER_IP;
        *opt++ = 4;
        memcpy(opt, meta.server_ip, 4);
        opt += 4;
    }
    *opt++ = OPT_END; // end
    
    return MIN_SEND_LEN;
}

void parse_frame(unsigned char *buf, int buflen, struct parse_meta *meta) {
    meta->dhcp_type = DHCP_UNKNOWN;
    meta->lease_time = 0;
    memset(meta->offered_ip, 0x0, 4);
    memset(meta->server_ip, 0x0, 4);
    memset(meta->subnet_mask, 0x0, 4);
    memset(meta->server_mac, 0x0, 6);

    if (buflen < MIN_RECV_LEN) 
        return;

    struct EthernetHdr *eth = (struct EthernetHdr *)buf;
    struct IPv4Hdr *ip = (struct IPv4Hdr *)(buf + ETHER_LEN);
    struct UDPHdr *udp = (struct UDPHdr *)(buf + ETHER_LEN + IPv4_LEN);
    struct DHCPHdr *dhcp_hdr = (struct DHCPHdr *)(buf + ETHER_LEN + IPv4_LEN + UDP_LEN);
    uint8_t * dhcp_options = buf + ETHER_LEN + IPv4_LEN + UDP_LEN + DHCPHDR_LEN;    

    // ethernet
    if (eth->etherType != htons(0x0800))
        return;

    // ip
    int opts_len = buflen - MIN_RECV_LEN;
    int udp_len = UDP_LEN + DHCPHDR_LEN + opts_len;
    int ip_len = sizeof(*ip) + udp_len;
    
    if (ip->protocol != 17 || ip->totalLen != htons(ip_len))
        return;

    uint32_t reported_cksum = ip->checksum; 
    ip->checksum = 0;
    if (reported_cksum != ip_cksum(ip, IPv4_LEN)) 
        return;

    // udp
    if (udp->srcPort != htons(67) || udp->dstPort != htons(68) 
            || udp->totalLen != htons(udp_len))
        return;

    // dhcp
    if (dhcp_hdr->xid != htonl(meta->xid))
        return;
    
    // 1. server MAC
    memcpy(meta->server_mac, eth->srcMAC, 6);

    // 2. server ip
    memcpy(meta->server_ip, dhcp_hdr->siaddr, 4);
    
    // 3. offered ip
    memcpy(meta->offered_ip, dhcp_hdr->yiaddr, 4);

    // 5. parse options
    int opt_idx = 0, opt_type = 0, opt_len = 0, i = 0; 
    while (opt_idx < opts_len) {
        opt_type = dhcp_options[opt_idx++];
        if (opt_type == OPT_PAD)
            continue;
        if (opt_type == OPT_END || opt_idx >= opts_len) 
            break; 
        opt_len = dhcp_options[opt_idx++];
        if (opt_idx + opt_len > opts_len)
            break;
        switch (opt_type) {
            case (OPT_MSG_TYPE):
                {
                    if (opt_len != 1) {
                        opt_idx += opt_len;
                        continue;
                    }

                    meta->dhcp_type = dhcp_options[opt_idx];
                    opt_idx += 1;
                }
                break;
            case (OPT_SERVER_IP):
                {
                    if (opt_len != 4) {
                        opt_idx += opt_len;
                        continue;
                    }

                    memcpy(meta->server_ip, dhcp_options + opt_idx, 4);
                    opt_idx += 4;
                }
                break;
            case (OPT_LEASE_TIME):
                {
                    if (opt_len != 4) {
                        opt_idx += opt_len;
                        continue;
                    }

                    meta->lease_time = ntohl(*(uint32_t *)(dhcp_options + opt_idx));
                    opt_idx += 4;
                }
                break;
            case (OPT_SUBNET_MASK): 
                {
                    if (opt_len != 4) {
                        opt_idx += opt_len;
                        continue;
                    }

                    memcpy(meta->subnet_mask, dhcp_options + opt_idx, 4);
                    opt_idx += 4;
                }
                break;
            default:
                opt_idx += opt_len;
                break;
        }
    }
}


int parse_xid(unsigned char *buf, int buflen, unsigned int *xid) {

    if (buflen < MIN_RECV_LEN) 
        return 0;

    struct EthernetHdr *eth = (struct EthernetHdr *)buf;
    struct IPv4Hdr *ip = (struct IPv4Hdr *)(buf + ETHER_LEN);
    struct UDPHdr *udp = (struct UDPHdr *)(buf + ETHER_LEN + IPv4_LEN);
    struct DHCPHdr *dhcp_hdr = (struct DHCPHdr *)(buf + ETHER_LEN + IPv4_LEN + UDP_LEN);
    uint8_t * dhcp_options = buf + ETHER_LEN + IPv4_LEN + UDP_LEN + DHCPHDR_LEN;    

    // ethernet
    if (eth->etherType != htons(0x0800))
        return 0;

    // ip
    int opts_len = buflen - MIN_RECV_LEN;
    int udp_len = UDP_LEN + DHCPHDR_LEN + opts_len;
    int ip_len = sizeof(*ip) + udp_len;
    
    if (ip->protocol != 17 || ip->totalLen != htons(ip_len))
        return 0;

    uint32_t reported_cksum = ip->checksum; 
    ip->checksum = 0;
    if (reported_cksum != ip_cksum(ip, IPv4_LEN)) { 
        ip->checksum = reported_cksum;
        return 0;
    }
    ip->checksum = reported_cksum;

    // udp
    if (udp->srcPort != htons(67) || udp->dstPort != htons(68) 
            || udp->totalLen != htons(udp_len))
        return 0;

    // dhcp
    *xid = ntohl(dhcp_hdr->xid);
    return 1;
 }

void debug_frame(unsigned char *buf, int buflen) {
    
    if (buflen < MIN_RECV_LEN) {
#ifdef DEBUG
        printf("DISCARD: length too short: expected %d, instead got %d\n",
              (int)MIN_RECV_LEN, buflen);
#endif
        return;
    }

    struct EthernetHdr *eth = (struct EthernetHdr *)buf;
    struct IPv4Hdr *ip = (struct IPv4Hdr *)(buf + ETHER_LEN);
    struct UDPHdr *udp = (struct UDPHdr *)(buf + ETHER_LEN + IPv4_LEN);
    struct DHCPHdr *dhcp_hdr = (struct DHCPHdr *)(buf + ETHER_LEN + IPv4_LEN + UDP_LEN);
    uint8_t * dhcp_options = buf + ETHER_LEN + IPv4_LEN + UDP_LEN + DHCPHDR_LEN;    

    // ethernet
    if (eth->etherType != htons(0x0800)) {
#ifdef DEBUG
        printf("DISCARD: expected ethernet type %x, instead got %x\n",
           0x0800, htons(eth->etherType));
#endif
        return;
    }

    // ip
    int opts_len = buflen - MIN_RECV_LEN;
    int udp_len = UDP_LEN + DHCPHDR_LEN + opts_len;
    int ip_len = sizeof(*ip) + udp_len;
    
    if (ip->protocol != 17) {
#ifdef DEBUG
        printf("DISCARD: expected ip protocol %d (UDP), instead got %d\n",
                17, ip->protocol);
#endif
        return;
    }
    if (ip->totalLen != htons(ip_len)) {
#ifdef DEBUG
        printf("DISCARD: expected ip totalLen to be %d, instead got %d\n",
                ip_len, htons(ip->totalLen));
#endif
        return;
    }

    uint32_t reported_cksum = ip->checksum; 
    ip->checksum = 0;
    if (reported_cksum != ip_cksum(ip, IPv4_LEN)) {
#ifdef DEBUG
        printf("DISCARD: expected checksum to be %x, instead got %x\n",
                ip_cksum(ip, IPv4_LEN), reported_cksum);
#endif
        return;
    }
    ip->checksum = reported_cksum;

    // udp
    if (udp->srcPort != htons(67) && udp->srcPort != htons(68)) {
#ifdef DEBUG
        printf("DISCARD: expected UDP src port to be 67 or 68, instead got %d\n",
                htons(udp->srcPort));
#endif
        return;
    }
    if (udp->dstPort != htons(67) && udp->dstPort != htons(68)) {
#ifdef DEBUG
        printf("DISCARD: expected UDP dst port to be 67 or 68, instead got %d\n",
                htons(udp->dstPort));
#endif
        return;
    }
    if (udp->totalLen != htons(udp_len)) {
#ifdef DEBUG
        printf("DISCARD: expected UDP totalLen to be %d, instead got %d\n",
                udp_len, htons(udp->totalLen));
#endif
        return;
    }

    // we arrive at dhcp! starts printing
    // 1. Ether header
    printf("Eth: src MAC %u::%u::%u::%u::%u::%u | dst MAC %u::%u::%u::%u::%u::%u\n | type %u\n",
            eth->srcMAC[0], eth->srcMAC[1], eth->srcMAC[2], eth->srcMAC[3], eth->srcMAC[4], eth->srcMAC[5],
            eth->dstMAC[0], eth->dstMAC[1], eth->dstMAC[2], eth->dstMAC[3], eth->dstMAC[4], eth->dstMAC[5],
            ntohs(eth->etherType));

    // 2. IP header
    printf("IPv4: versionIhl %u | tos %u | len %u | ident %u\n",
            ip->versionIhl, ip->tos, htons(ip->totalLen), htons(ip->ident));
    printf("IPv4: flags %u | ttl %u | protocol %u | checksum %u\n",
            htons(ip->flags), ip->ttl, ip->protocol, htons(ip->checksum));
    printf("ipv4: src %d.%d.%d.%d | dst %d.%d.%d.%d\n",
            ip->srcIP[0], ip->srcIP[1], ip->srcIP[2], ip->srcIP[3],
            ip->dstIP[0], ip->dstIP[1], ip->dstIP[2], ip->dstIP[3]);

    // 3. UDP header
    printf("UDP: src port %u | dst port %u | len %u | checksum %u\n",
            ntohs(udp->srcPort), ntohs(udp->dstPort), ntohs(udp->totalLen), ntohs(udp->checksum));

    // 4. DHCP header
    printf("DHCP: op %u | htype %u | hlen %u | hops %u | xid %x\n",
            dhcp_hdr->op, dhcp_hdr->htype, dhcp_hdr->hlen, dhcp_hdr->hops, ntohl(dhcp_hdr->xid));
    printf("DHCP: ciaddr %d.%d.%d.%d | yiaddr %d.%d.%d.%d\n",
            dhcp_hdr->ciaddr[0], dhcp_hdr->ciaddr[1], dhcp_hdr->ciaddr[2], dhcp_hdr->ciaddr[3],
            dhcp_hdr->yiaddr[0], dhcp_hdr->yiaddr[1], dhcp_hdr->yiaddr[2], dhcp_hdr->yiaddr[3]);
    printf("DHCP: siaddr %d.%d.%d.%d | giaddr %d.%d.%d.%d\n",
            dhcp_hdr->siaddr[0], dhcp_hdr->siaddr[1], dhcp_hdr->siaddr[2], dhcp_hdr->siaddr[3],
            dhcp_hdr->giaddr[0], dhcp_hdr->giaddr[1], dhcp_hdr->giaddr[2], dhcp_hdr->giaddr[3]);

    // 5. print options
    int opt_idx = 0, opt_type = 0, opt_len = 0, i = 0; 
    while (opt_idx < opts_len) {
        opt_type = dhcp_options[opt_idx++];
        if (opt_type == OPT_PAD)
            continue;
        if (opt_type == OPT_END || opt_idx >= opts_len) 
            break; 
        opt_len = dhcp_options[opt_idx++];
        if (opt_idx + opt_len > opts_len)
            break;
        printf("DHCP_OPT: type %u | len %u | content ",
                opt_type, opt_len);
        for (i = 0; i < opt_len; i++) 
            printf("%x ", dhcp_options[opt_idx++]);
        printf("\n");
    }
}


