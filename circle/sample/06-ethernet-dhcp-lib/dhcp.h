//
// dhcp.h
//
// CDHCPServer backed by dhcp-lib (portable C DHCP library).
// Compile-time lease mode selection via:
//   -DDHCP_LEASE_MODE_ARRAY      (default) per-MAC lease array
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

// Default to ARRAY mode when no mode is explicitly selected at compile time.
#if !defined(DHCP_LEASE_MODE_ARRAY) && \
    !defined(DHCP_LEASE_MODE_BMVAR) && \
    !defined(DHCP_LEASE_MODE_BMUNI) && \
    !defined(DHCP_LEASE_MODE_NPRC) && \
    !defined(DHCP_LEASE_MODE_HASHMAP) 
#define DHCP_LEASE_MODE_ARRAY
#endif

#define DHCP_MAX_LEASES      10
#define DHCP_BMUNI_NUM_RANGES 4u

// Circle server IP pool: 192.168.4.100 – 192.168.255.255
#define DHCP_POOL_START  0xC0A80464u
#define DHCP_POOL_END    0xC0A8FFFFu
#define DHCP_POOL_SIZE   (DHCP_POOL_END - DHCP_POOL_START + 1u)

class CDHCPServer
{
public:
    CDHCPServer (CNetDevice *pNetDevice);
    ~CDHCPServer (void);

    u8 ProcessDHCPHdr (const DHCPHdr *pDHCP, unsigned nLength);
    unsigned CraftDHCPOffer (const DHCPHdr *pRequest, DHCPHdr *pResponse);
    unsigned CraftDHCPAck (const DHCPHdr *pRequest, unsigned requestLen, DHCPHdr *pResponse);
    void HandleDHCPDecline(const DHCPHdr *pDHCP);

private:
    CNetDevice   *m_pNetDevice;
    dhcp_server_t m_server;

#ifdef DHCP_LEASE_MODE_ARRAY
    dhcp_lease_t   m_leases[DHCP_MAX_LEASES];
#endif
#ifdef DHCP_LEASE_MODE_BMVAR
    dhcp_bmrange_t m_bmvar_range;
    uint32_t       m_bmvar_ips[(DHCP_POOL_SIZE + 31u) / 32u];
#endif
#ifdef DHCP_LEASE_MODE_BMUNI
    dhcp_bmrange_t m_bmuni_ranges[DHCP_BMUNI_NUM_RANGES];
    uint32_t       m_bmuni_ips[DHCP_BMUNI_NUM_RANGES]
                               [(DHCP_POOL_SIZE / DHCP_BMUNI_NUM_RANGES + 31u) / 32u];
#endif

    // Wire-format <-> library message converters (implemented in dhcp-server.cpp).
    static void hdrToMsg (const DHCPHdr *pHdr, unsigned nLength, dhcp_message_t *pMsg);
    static void msgToHdr (const dhcp_message_t *pMsg, DHCPHdr *pHdr, unsigned *pLength);
};

#endif
