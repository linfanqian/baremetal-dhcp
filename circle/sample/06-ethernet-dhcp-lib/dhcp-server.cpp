//
// dhcp-server.cpp
//
// Circle adapter: bridges Circle's packed wire-format DHCPHdr with the
// portable dhcp-lib (dhcp_message_t / dhcp_server_t) API.
//
// Conversion rules:
//   DHCPHdr.magic    — u32 stored little-endian in a PACKED struct on a
//                      little-endian CPU; use BE32() to reach host order.
//   DHCPHdr.*addr    — u8[4] big-endian arrays; unpackIP/packIP convert
//                      to/from host-order uint32_t used by the library.
//   DHCPHdr.xid      — opaque u32; pass through as-is (no byte swap).
//

#include "dhcp.h"
#include "mystring.h"
#include <circle/logger.h>
#include <circle/timer.h>

#define DHCP_FIXED_LEN 240      // 236-byte fixed BOOTP header + 4-byte magic cookie

static const char FromDHCPServer[] = "dhcpserver";

// ─── Wire-format helpers ──────────────────────────────────────────────────────

// u8[4] big-endian (network byte order) → host-order uint32_t
static uint32_t unpackIP (const u8 *p)
{
    return ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8)
         |  (uint32_t)p[3];
}

// host-order uint32_t → u8[4] big-endian (network byte order)
static void packIP (uint32_t ip, u8 *p)
{
    p[0] = (ip >> 24) & 0xFF;
    p[1] = (ip >> 16) & 0xFF;
    p[2] = (ip >>  8) & 0xFF;
    p[3] =  ip        & 0xFF;
}

// ─── CDHCPServer::hdrToMsg ───────────────────────────────────────────────────
//
// Convert a Circle wire-format DHCPHdr to dhcp-lib's in-memory dhcp_message_t.
// nLength is the total DHCP payload length (fixed header + options).
//
void CDHCPServer::hdrToMsg (const DHCPHdr *pHdr, unsigned nLength,
                             dhcp_message_t *pMsg)
{
    my_memset (pMsg, 0, sizeof (dhcp_message_t));

    pMsg->op    = pHdr->op;
    pMsg->htype = pHdr->htype;
    pMsg->hlen  = pHdr->hlen;
    pMsg->hops  = pHdr->hops;
    pMsg->xid   = pHdr->xid;   // opaque — no byte swap
    pMsg->secs  = pHdr->secs;
    pMsg->flags = pHdr->flags;

    pMsg->ciaddr = unpackIP (pHdr->ciaddr);
    pMsg->yiaddr = unpackIP (pHdr->yiaddr);
    pMsg->siaddr = unpackIP (pHdr->siaddr);
    pMsg->giaddr = unpackIP (pHdr->giaddr);

    my_memcpy (pMsg->chaddr, pHdr->chaddr, 16);

    // DHCPHdr.magic is a PACKED u32 stored in little-endian on wire.
    // BE32() byte-swaps it to host order, yielding 0x63825363.
    pMsg->magic_cookie = BE32 (pHdr->magic);

    // Copy options, capped at dhcp_message_t.options capacity (308 bytes).
    unsigned optLen = (nLength > DHCP_FIXED_LEN) ? (nLength - DHCP_FIXED_LEN) : 0;
    if (optLen > sizeof (pMsg->options))
        optLen = sizeof (pMsg->options);
    my_memcpy (pMsg->options, pHdr->options, optLen);
}

// ─── CDHCPServer::msgToHdr ───────────────────────────────────────────────────
//
// Convert a dhcp-lib dhcp_message_t back to a Circle wire-format DHCPHdr.
// *pLength is set to the total DHCP packet length (fixed header + options).
//
void CDHCPServer::msgToHdr (const dhcp_message_t *pMsg, DHCPHdr *pHdr,
                             unsigned *pLength)
{
    my_memset (pHdr, 0, sizeof (DHCPHdr));

    pHdr->op    = pMsg->op;
    pHdr->htype = pMsg->htype;
    pHdr->hlen  = pMsg->hlen;
    pHdr->hops  = pMsg->hops;
    pHdr->xid   = pMsg->xid;   // opaque — no byte swap
    pHdr->secs  = pMsg->secs;
    pHdr->flags = pMsg->flags;

    packIP (pMsg->ciaddr, pHdr->ciaddr);
    packIP (pMsg->yiaddr, pHdr->yiaddr);
    packIP (pMsg->siaddr, pHdr->siaddr);
    packIP (pMsg->giaddr, pHdr->giaddr);

    my_memcpy (pHdr->chaddr, pMsg->chaddr, 16);

    // Reverse the magic cookie conversion: host-order → LE u32 on wire.
    pHdr->magic = BE32 (pMsg->magic_cookie);

    // Determine options length by scanning for the END marker (0xFF).
    unsigned optLen = 0;
    const u8 *opts = pMsg->options;
    for (unsigned i = 0; i < sizeof (pMsg->options); i++)
    {
        optLen++;
        if (opts[i] == 0xFF)    // DHCP_OPT_END
            break;
    }
    my_memcpy (pHdr->options, pMsg->options, optLen);

    *pLength = DHCP_FIXED_LEN + optLen;
}

// ─── CDHCPServer::CDHCPServer ────────────────────────────────────────────────

CDHCPServer::CDHCPServer (CNetDevice *pNetDevice)
    : m_pNetDevice (pNetDevice)
{
    assert (pNetDevice != 0);

    dhcp_config_t config;
    config.server_ip   = 0xC0A80401u;  // 192.168.4.1
    config.gateway_ip  = 0xC0A80401u;  // same as server
    config.subnet_mask = 0xFFFFFF00u;  // 255.255.255.0
    config.dns_ip      = 0xC0A80401u;  // point DNS at the server for now
    config.pool_start  = 0xC0A80464u;  // 192.168.4.100
    config.pool_end    = 0xC0A8FFFFu;  // 192.168.255.255
    config.lease_time  = 60;         // 1 hour

    CLogger *pLogger = CLogger::Get ();

#if defined(DHCP_LEASE_MODE_ARRAY)
    dhcp_init_server_array (&m_server, &config, m_leases, DHCP_MAX_LEASES);
    if (pLogger)
        pLogger->Write (FromDHCPServer, LogNotice,
                        "DHCP server initialized (ARRAY mode)");
#elif defined(DHCP_LEASE_MODE_BMVAR)
    dhcp_init_server_bmvar (&m_server, &config);
    if (pLogger)
        pLogger->Write (FromDHCPServer, LogNotice,
                        "DHCP server initialized (BITMAP_VARTIME mode)");
#elif defined(DHCP_LEASE_MODE_BMUNI)
    dhcp_init_server_bmuni (&m_server, &config);
    if (pLogger)
        pLogger->Write (FromDHCPServer, LogNotice,
                        "DHCP server initialized (BITMAP_UNITIME mode)");
#elif defined(DHCP_LEASE_MODE_NPRC)
    dhcp_init_server_nprc (&m_server, &config);
#elif defined(DHCP_LEASE_MODE_HASHMAP)
    dhcp_init_server_hashmap (&m_server, &config);
    if (pLogger)
        pLogger->Write (FromDHCPServer, LogNotice,
                        "DHCP server initialized (HASHMAP mode)");
#endif

    if (pLogger)
    {
        pLogger->Write (FromDHCPServer, LogNotice, "DHCP server configured at %d.%d.%d.%d", 
						(config.server_ip >> 24) & 0xff, (config.server_ip >> 16) & 0xff,
                        (config.server_ip >> 8) & 0xff, config.server_ip & 0xff);
		pLogger->Write (FromDHCPServer, LogNotice, "DHCP gateway configured at %d.%d.%d.%d", 
						(config.gateway_ip >> 24) & 0xff, (config.gateway_ip >> 16) & 0xff,
                        (config.gateway_ip >> 8) & 0xff, config.gateway_ip & 0xff);
		pLogger->Write (FromDHCPServer, LogNotice, "DHCP subnet mask configured at %d.%d.%d.%d", 
						(config.subnet_mask >> 24) & 0xff, (config.subnet_mask >> 16) & 0xff,
                        (config.subnet_mask >> 8) & 0xff, config.subnet_mask & 0xff);
		pLogger->Write (FromDHCPServer, LogNotice, "DHCP network pool configured at %d.%d.%d.%d", 
						(config.dns_ip >> 24) & 0xff, (config.dns_ip >> 16) & 0xff,
                        (config.dns_ip >> 8) & 0xff, config.dns_ip & 0xff);
		pLogger->Write (FromDHCPServer, LogNotice, "DHCP network pool configured start at %d.%d.%d.%d", 
						(config.pool_start >> 24) & 0xff, (config.pool_start >> 16) & 0xff,
                        (config.pool_start >> 8) & 0xff, config.pool_start & 0xff);
		pLogger->Write (FromDHCPServer, LogNotice, "DHCP network pool configured end at %d.%d.%d.%d", 
						(config.pool_end >> 24) & 0xff, (config.pool_end >> 16) & 0xff,
                        (config.pool_end >> 8) & 0xff, config.pool_end & 0xff);
    }
}

CDHCPServer::~CDHCPServer (void)
{
}

// ─── CDHCPServer::ProcessDHCPHdr ─────────────────────────────────────────────
//
// Validate the incoming DHCP packet and return its message type.
// Returns 0 on error (too short, bad magic cookie).
//
u8 CDHCPServer::ProcessDHCPHdr (const DHCPHdr *pDHCP, unsigned nLength)
{
    CLogger *pLogger = CLogger::Get ();

    if (nLength < DHCP_FIXED_LEN)
    {
        if (pLogger)
            pLogger->Write (FromDHCPServer, LogWarning,
                            "DHCP packet too small: expected %d, got %d bytes",
                            DHCP_FIXED_LEN, nLength);
        return 0;
    }

    // Verify magic cookie (BE32 converts the PACKED LE u32 to host order).
    u32 magic = BE32 (pDHCP->magic);
    if (magic != DHCP_MAGIC_COOKIE)
    {
        if (pLogger)
            pLogger->Write (FromDHCPServer, LogWarning,
                            "Invalid DHCP magic cookie: expected %x, got %x",
                            DHCP_MAGIC_COOKIE, magic);
        return 0;
    }

    // Convert to library message to extract the message type option.
    dhcp_message_t msg;
    hdrToMsg (pDHCP, nLength, &msg);
    u8 msgType = dhcp_get_message_type (&msg);

    if (pLogger)
    {
        const char *typeName = "UNKNOWN";
        switch (msgType)
        {
            case DHCP_DISCOVER: typeName = "DISCOVER"; break;
            case DHCP_OFFER:    typeName = "OFFER";    break;
            case DHCP_REQUEST:  typeName = "REQUEST";  break;
            case DHCP_ACK:      typeName = "ACK";      break;
            case DHCP_NAK:      typeName = "NAK";      break;
            default:            break;
        }
        pLogger->Write (FromDHCPServer, LogNotice,
                        "Received DHCP %s from MAC %X:%X:%X:%X:%X:%X",
                        typeName,
                        msg.chaddr[0], msg.chaddr[1], msg.chaddr[2],
                        msg.chaddr[3], msg.chaddr[4], msg.chaddr[5]);
    }

    return msgType;
}

// ─── CDHCPServer::CraftDHCPOffer ─────────────────────────────────────────────
//
// Process a DHCP DISCOVER and build an OFFER into pResponse.
// Returns total DHCP packet length, or 0 if no offer could be built.
//
unsigned CDHCPServer::CraftDHCPOffer (const DHCPHdr *pRequest,
                                       DHCPHdr *pResponse)
{
    CLogger *pLogger = CLogger::Get ();
    
    dhcp_message_t req, resp;
    hdrToMsg (pRequest, sizeof (DHCPHdr), &req);
    my_memset (&resp, 0, sizeof (resp));

#if defined(DHCP_LEASE_MODE_ARRAY)
    u32 ts_in_sec = CTimer::Get ()->GetClockTicks () / 1000000;
    dhcp_process_message_array (&m_server, &req, &resp, ts_in_sec);
#elif defined(DHCP_LEASE_MODE_BMVAR)
    u32 ts_in_sec = CTimer::Get ()->GetClockTicks () / 1000000;
    dhcp_process_message_bmvar (&m_server, &req, &resp, ts_in_sec);
#elif defined(DHCP_LEASE_MODE_BMUNI)
    u32 ts_in_sec = CTimer::Get ()->GetClockTicks () / 1000000;
    dhcp_process_message_bmuni (&m_server, &req, &resp, ts_in_sec);
#elif defined(DHCP_LEASE_MODE_NPRC)
    dhcp_process_message_nprc(&m_server, &req, &resp);
#elif defined(DHCP_LEASE_MODE_HASHMAP)
    u32 ts_in_sec = CTimer::Get ()->GetClockTicks () / 1000000;
    dhcp_process_message_hashmap (&m_server, &req, &resp, ts_in_sec);
#endif

    // resp.op == 0 means the library built no response (e.g. pool full).
    if (resp.op == 0)
        return 0;
    
    if (pLogger) {
        pLogger->Write (FromDHCPServer, LogDebug,
                        "Lease offered and recorded: %d.%d.%d.%d for MAC Address %X::%X::%X::%X::%X::%X",
                        (resp.yiaddr >> 24) & 0xff, (resp.yiaddr >> 16) & 0xff,
                        (resp.yiaddr >> 8) & 0xff, resp.yiaddr & 0xff,
                        pRequest->chaddr[0], pRequest->chaddr[1], pRequest->chaddr[2], 
                        pRequest->chaddr[3], pRequest->chaddr[4], pRequest->chaddr[5]);
    }

    unsigned len;
    msgToHdr (&resp, pResponse, &len);
    return len;
}

// ─── CDHCPServer::CraftDHCPAck ───────────────────────────────────────────────
//
// Process a DHCP REQUEST and build an ACK or NAK into pResponse.
// Returns total DHCP packet length, or 0 if no response was built.
//
unsigned CDHCPServer::CraftDHCPAck (const DHCPHdr *pRequest,
                                     unsigned requestLen,
                                     DHCPHdr *pResponse)
{
    CLogger *pLogger = CLogger::Get ();

    dhcp_message_t req, resp;
    // Pass the actual packet length so option 50 (Requested IP) is in scope.
    hdrToMsg (pRequest, requestLen, &req);
    my_memset (&resp, 0, sizeof (resp));

#if defined(DHCP_LEASE_MODE_ARRAY)
    u32 ts_in_sec = CTimer::Get ()->GetClockTicks () / 1000000;
    dhcp_process_message_array (&m_server, &req, &resp, ts_in_sec);
#elif defined(DHCP_LEASE_MODE_BMVAR)
    u32 ts_in_sec = CTimer::Get ()->GetClockTicks () / 1000000;
    dhcp_process_message_bmvar (&m_server, &req, &resp, ts_in_sec);
#elif defined(DHCP_LEASE_MODE_BMUNI)
    u32 ts_in_sec = CTimer::Get ()->GetClockTicks () / 1000000;
    dhcp_process_message_bmuni (&m_server, &req, &resp, ts_in_sec);
#elif defined(DHCP_LEASE_MODE_NPRC)
    dhcp_process_message_nprc (&m_server, &req, &resp);
#elif defined(DHCP_LEASE_MODE_HASHMAP)
    u32 ts_in_sec = CTimer::Get ()->GetClockTicks () / 1000000;
    dhcp_process_message_hashmap (&m_server, &req, &resp, ts_in_sec);
#endif

    if (resp.op == 0)
        return 0;

    // if (pLogger && dhcp_get_message_type (&resp) == DHCP_ACK)
    //     pLogger->Write (FromDHCPServer, LogNotice,
    //                     "IP %d.%d.%d.%d accepted",
    //                     (resp.yiaddr >> 24) & 0xff, (resp.yiaddr >> 16) & 0xff,
    //                     (resp.yiaddr >> 8) & 0xff, resp.yiaddr & 0xff);
    u8 respType = dhcp_get_message_type(&resp);
    if (pLogger) {
        if (respType == DHCP_ACK)
            pLogger->Write(FromDHCPServer, LogNotice,
                        "Sending ACK: IP %d.%d.%d.%d assigned",
                        (resp.yiaddr >> 24) & 0xff, (resp.yiaddr >> 16) & 0xff,
                        (resp.yiaddr >> 8) & 0xff, resp.yiaddr & 0xff);
        else if (respType == DHCP_NAK)
            pLogger->Write(FromDHCPServer, LogNotice, "Sending NAK");
    }

    unsigned len;
    msgToHdr (&resp, pResponse, &len);
    return len;
}
