#ifndef _debug_frame_h
#define _debug_frame_h

#include "frame.h"
#include "dhcpserver.h"
#include <circle/logger.h>

void logEthernetHdr(EthernetHdr *eth, CLogger *logger);
void logIPv4Hdr(IPv4Hdr *ipv4, CLogger *logger);
void logUDPHdr(UDPHdr *udp, CLogger *logger);
void logDHCPHdr(DHCPPacket *dhcp, CLogger *logger);

#endif
