#ifndef __HEADER_H__
#define __HEADER_H__

#include <stdint.h>

#ifdef __GNUC__
#define PACKED __attribute__ ((packed))
#else
#define PACKED
#endif

struct EthernetHdr {
    uint8_t  dstMAC[6];
    uint8_t  srcMAC[6];
    uint16_t etherType;
} PACKED;

struct IPv4Hdr {
    uint8_t  versionIhl;
    uint8_t  tos;
    uint16_t totalLen;
    uint16_t ident;
    uint16_t flags;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  srcIP[4];
    uint8_t  dstIP[4];
} PACKED;

struct UDPHdr {
    uint16_t srcPort;
    uint16_t dstPort;
    uint16_t totalLen;
    uint16_t checksum;
} PACKED;

struct DHCPHdr
{
	uint8_t  op;			// 1=request, 2=reply
	uint8_t  htype;		// hardware type (1=Ethernet)
	uint8_t  hlen;		// hardware length
	uint8_t  hops;
	uint32_t xid;		// transaction ID
	uint16_t secs;
	uint16_t flags;
	uint8_t ciaddr[4];		// client IP
	uint8_t yiaddr[4];		// your IP (offered)
	uint8_t siaddr[4];		// server IP
	uint8_t giaddr[4];		// gateway IP
	uint8_t  chaddr[16];	// client hardware address
	uint8_t  sname[64];	// server name
	uint8_t  file[128];	// boot file
	uint32_t magic;		// magic cookie (0x63825363)
} PACKED;

struct DHCP
{
    struct DHCPHdr hdr;
	uint8_t  options[312];	// options
} PACKED;

#define ETHER_LEN       (sizeof(struct EthernetHdr))
#define IPv4_LEN        (sizeof(struct IPv4Hdr))
#define UDP_LEN         (sizeof(struct UDPHdr))
#define DHCPHDR_LEN     (sizeof(struct DHCPHdr))
#define DHCP_LEN        (sizeof(struct DHCP)) 

// receive frame does not need to have all options
#define MIN_RECV_LEN (ETHER_LEN + IPv4_LEN + UDP_LEN + DHCPHDR_LEN)
#define MIN_SEND_LEN (ETHER_LEN + IPv4_LEN + UDP_LEN + DHCP_LEN)

#endif
