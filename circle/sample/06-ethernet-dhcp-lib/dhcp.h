//
// dhcp.h
//
// CDHCPServer backed by dhcp-lib (portable C DHCP library).
// Compile-time lease mode selection via:
//   -DDHCP_LEASE_MODE_TABLE      (default) per-MAC lease table
//   -DDHCP_LEASE_MODE_BMVAR      bitmap pool, variable lease time
//   -DDHCP_LEASE_MODE_BMUNI      bitmap pool, unified lease time
//

#ifndef _dhcp_h
#define _dhcp_h

#include "headers.h"
#include <circle/netdevice.h>
#include <circle/types.h>

// Pull in the dhcp-lib C API. 
extern "C" {
#include "dhcp_server.h"
}

// Default to TABLE mode when no mode is explicitly selected at compile time.
#if !defined(DHCP_LEASE_MODE_TABLE) && \
    !defined(DHCP_LEASE_MODE_BMVAR) && \
    !defined(DHCP_LEASE_MODE_BMUNI) && \
    !defined(DHCP_LEASE_MODE_NPRC) && \
    !defined(DHCP_LEASE_MODE_HASHMAP) 
#define DHCP_LEASE_MODE_TABLE
#endif

#define DHCP_MAX_LEASES 10

class CDHCPServer
{
public:
    CDHCPServer (CNetDevice *pNetDevice);
    ~CDHCPServer (void);

    u8 ProcessDHCPHdr (const DHCPHdr *pDHCP, unsigned nLength);
    unsigned CraftDHCPOffer (const DHCPHdr *pRequest, DHCPHdr *pResponse);
    unsigned CraftDHCPAck (const DHCPHdr *pRequest, unsigned requestLen, DHCPHdr *pResponse);

private:
    CNetDevice   *m_pNetDevice;
    dhcp_server_t m_server;

#ifdef DHCP_LEASE_MODE_TABLE
    // TABLE mode requires a caller-supplied lease storage array.
    dhcp_lease_t  m_leases[DHCP_MAX_LEASES];
#endif

    // Wire-format <-> library message converters (implemented in dhcp-server.cpp).
    static void hdrToMsg (const DHCPHdr *pHdr, unsigned nLength, dhcp_message_t *pMsg);
    static void msgToHdr (const dhcp_message_t *pMsg, DHCPHdr *pHdr, unsigned *pLength);
};

#endif
