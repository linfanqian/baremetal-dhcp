#ifndef _frame_h
#define _frame_h

#include <circle/types.h>
#include <circle/string.h>

#ifdef __GNUC__
#define PACKED __attribute__ ((packed))
#else
#define PACKED
#endif


#define BE32(x) (((u32)(x) >> 24) & 0xFF) | (((u32)(x) >> 8) & 0xFF00) | \
                (((u32)(x) << 8) & 0xFF0000) | (((u32)(x) << 24) & 0xFF000000)

struct EthernetHdr {
    u8  dstMAC[6];
    u8  srcMAC[6];
    u16 etherType;
} PACKED;

struct IPv4Hdr {
    u8  versionIhl;
    u8  tos;
    u16 totalLen;
    u16 ident;
    u16 flags;
    u8  ttl;
    u8  protocol;
    u16 checksum;
    u8  srcIP[4];
    u8  dstIP[4];
} PACKED;

struct UDPHdr {
    u16 srcPort;
    u16 dstPort;
    u16 totalLen;
    u16 checksum;
} PACKED;

struct DHCPHdr
{
	u8  op;			// 1=request, 2=reply
	u8  htype;		// hardware type (1=Ethernet)
	u8  hlen;		// hardware length
	u8  hops;
	u32 xid;		// transaction ID
	u16 secs;
	u16 flags;
	u8 ciaddr[4];		// client IP
	u8 yiaddr[4];		// your IP (offered)
	u8 siaddr[4];		// server IP
	u8 giaddr[4];		// gateway IP
	u8  chaddr[16];	// client hardware address
	u8  sname[64];	// server name
	u8  file[128];	// boot file
	u32 magic;		// magic cookie (0x63825363)
	u8  options[312];	// options
} PACKED;

#endif
