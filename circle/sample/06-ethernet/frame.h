#ifndef _frame_h
#define _frame_h

#include <circle/types.h>
#include <circle/string.h>
#include "dhcpserver.h"

#ifdef __GNUC__
#define PACKED __attribute__ ((packed))
#else
#define PACKED
#endif

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

CString processSrc(const u8 *pFrame, unsigned nLen);
CString processData(const u8 *pFrame, unsigned nLen, CDHCPServer *serv);

#endif
