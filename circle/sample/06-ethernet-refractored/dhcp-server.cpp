//
// dhcpserver.cpp
//
// Minimal DHCP server implementation for Circle
//

#include "mystring.h"
#include "headers.h"
#include "dhcp.h"
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/timer.h>
#include <circle/sched/scheduler.h>
#include <circle/net/in.h>
#include <circle/debug.h>
#include <circle/macaddress.h>

/*
 * net related modules commented out here 
#include <circle/net/in.h>
*/

// #define DEBUG

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_FIXED_LEN 240 // 236 standard fixed header + 4 magic cookie
#define DHCP_MAGIC 0x63825363

static const char FromDHCPServer[] = "dhcpserver";

static void u32ToBytes(u32 val, u8 *buf)
{
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8)  & 0xFF;
    buf[3] =  val        & 0xFF;
}

static u32 BytesTou32(const u8 *buf)
{
    return ((u32)buf[0] << 24)
         | ((u32)buf[1] << 16)
         | ((u32)buf[2] << 8)
         |  (u32)buf[3];
}

CDHCPServer::CDHCPServer (CNetDevice *pNetDevice)
	: 
      // CTask (8192, "DHCPServer"),
	  m_pNetDevice (pNetDevice),
	  m_nServerIP (0), m_nSubnetMask (0),
	  m_nGatewayIP (0),m_nDHCPPoolStart (0),
	  m_nDHCPPoolEnd (0), m_nLeaseTime (3600),	// 1 hour default
	  m_nNextLeaseIndex (0)
{
	assert (pNetDevice != 0);
	
	// Initialize leases
	for (unsigned i = 0; i < 10; i++)
	{
		m_aLeases[i].inUse = FALSE;
		m_aLeases[i].ipaddr = 0;
		m_aLeases[i].leaseStart = 0;
	}

	// Get server IP - use operator to convert CIPAddress to u32
	m_nServerIP   = 0xc0a80401;  // 192.168.4.1
	m_nGatewayIP  = 0xc0a80401;  // Csame as server for now
	m_nSubnetMask = 0xffffff00;  // 255.255.255.0
	u32 nNetwork = m_nServerIP & m_nSubnetMask;
	m_nDHCPPoolStart = nNetwork | 100;
	m_nDHCPPoolEnd = nNetwork | 200;
	
	// Log configuration
	CLogger *pLogger = CLogger::Get ();
	if (pLogger)
	{
        // TODO: this is just to verify the hard-coded configuration
        // we can probably do something else later
		pLogger->Write (FromDHCPServer, LogNotice, "DHCP server configured at %d.%d.%d.%d", 
						(m_nServerIP >> 24) & 0xff, (m_nServerIP >> 16) & 0xff,
                        (m_nServerIP >> 8) & 0xff, m_nServerIP & 0xff);
		pLogger->Write (FromDHCPServer, LogNotice, "DHCP gateway configured at %d.%d.%d.%d", 
						(m_nGatewayIP >> 24) & 0xff, (m_nGatewayIP >> 16) & 0xff,
                        (m_nGatewayIP >> 8) & 0xff, m_nGatewayIP & 0xff);
		pLogger->Write (FromDHCPServer, LogNotice, "DHCP subnet mask configured at %d.%d.%d.%d", 
						(m_nSubnetMask >> 24) & 0xff, (m_nSubnetMask >> 16) & 0xff,
                        (m_nSubnetMask >> 8) & 0xff, m_nSubnetMask & 0xff);
		pLogger->Write (FromDHCPServer, LogNotice, "DHCP network pool configured at %d.%d.%d.%d", 
						(nNetwork >> 24) & 0xff, (nNetwork >> 16) & 0xff,
                        (nNetwork >> 8) & 0xff, nNetwork & 0xff);
		pLogger->Write (FromDHCPServer, LogNotice, "DHCP network pool configured start at %d.%d.%d.%d", 
						(m_nDHCPPoolStart >> 24) & 0xff, (m_nDHCPPoolStart >> 16) & 0xff,
                        (m_nDHCPPoolStart >> 8) & 0xff, m_nDHCPPoolStart & 0xff);
		pLogger->Write (FromDHCPServer, LogNotice, "DHCP network pool configured end at %d.%d.%d.%d", 
						(m_nDHCPPoolEnd >> 24) & 0xff, (m_nDHCPPoolEnd >> 16) & 0xff,
                        (nNetwork >> 8) & 0xff, m_nDHCPPoolEnd & 0xff);

	}
}

CDHCPServer::~CDHCPServer (void)
{
    // TODO: we currently don't have any structure that needs to be destructed now
    // previously there is UDP socket, but we are not using the data structure here
}

u8 CDHCPServer::ProcessDHCPHdr (const DHCPHdr *pDHCP, unsigned nLength)
{
	CLogger *pLogger = CLogger::Get ();
	
	if (nLength < DHCP_FIXED_LEN)
	{
		if (pLogger)
			pLogger->Write (FromDHCPServer, LogWarning, "DHCP packet too small: expected %d, got %d bytes instead", DHCP_FIXED_LEN, nLength);

		return 0;
	}

	// Verify magic cookie
    u32 magic_cookie = BE32(pDHCP->magic);
	if (magic_cookie != DHCP_MAGIC)
	{
		if (pLogger)
			pLogger->Write (FromDHCPServer, LogWarning, "Invalid DHCP magic cookie: expected %x but instead got %x", DHCP_MAGIC, magic_cookie);
        
		return 0;
	}
	
	// Get message type
    boolean res;
    u8 nMessageType;
	res = GetDHCPOptionType (pDHCP->options, nLength - DHCP_FIXED_LEN,
										 DHCP_OPTION_MESSAGE_TYPE, 
                                         1, &nMessageType); // only need 1 byte here
    if (res == FALSE) {
        nMessageType = 0; // make sure it is invalid
    }

	if (pLogger)
	{
		CString pTypeName ("UNKNOWN");
		switch (nMessageType)
		{
			case DHCP_DISCOVER: pTypeName = "DISCOVER"; break;
			case DHCP_REQUEST:  pTypeName = "REQUEST"; break;
			case DHCP_OFFER:    pTypeName = "OFFER"; break;
			case DHCP_ACKNOWLEDGE: pTypeName = "ACK"; break;
            case DHCP_NAK: pTypeName = "NAK"; break;
            default: break;
		}
    pLogger->Write(FromDHCPServer, LogNotice, "received DHCP %s packet", (const char *)pTypeName);
	
    }
	
    return nMessageType;
}

unsigned CDHCPServer::CraftDHCPOffer (const DHCPHdr *pRequest, DHCPHdr *pResponse)
{
    CLogger *pLogger = CLogger::Get ();

    u32 nOfferedIP;
    boolean rc;
                   
    
    if ((rc = FindLease (pRequest->chaddr, nOfferedIP)) == FALSE) {
	    // Allocate IP address
	    nOfferedIP = AssignIP (pRequest->chaddr);
        RecordLease (pRequest->chaddr, nOfferedIP);

        
        // TODO: do we need a list of pending offers?
    }

    if (pLogger) {
		pLogger->Write (FromDHCPServer, LogDebug,
					   "Lease offered and recorded: %d.%d.%d.%d for MAC Address %u::%u::%u::%u::%u::%u",
					   (nOfferedIP >> 24) & 0xff, (nOfferedIP >> 16) & 0xff,
                       (nOfferedIP >> 8) & 0xff, nOfferedIP & 0xff,
                       pRequest->chaddr[0], pRequest->chaddr[1], pRequest->chaddr[2], 
                       pRequest->chaddr[3], pRequest->chaddr[4], pRequest->chaddr[5]);
    }
	
	// Build DHCP response packet
    // TODO: check these chunk of assignment
	pResponse->op = 2;			// BOOTREPLY
	pResponse->htype = pRequest->htype;
	pResponse->hlen = pRequest->hlen;
	pResponse->hops = 0;
	pResponse->xid = pRequest->xid;
	pResponse->secs = 0;
	pResponse->flags = pRequest->flags;
	pResponse->magic = BE32(DHCP_MAGIC); // Set magic cookie
    my_memset(pResponse->ciaddr, 0, 4); // initialize client IP address to 0
    // my_memset(pResponse->siaddr, 0, 4); // TODO: siaddr is meant for next-server (TFTP), which we don't really have now?
    my_memcpy(pResponse->giaddr, pRequest->giaddr, 4); // echo relay agent back
	my_memcpy (pResponse->chaddr, pRequest->chaddr, 16); // client MAC address should be the same for request and response?
                                                        
    u32ToBytes(nOfferedIP, pResponse->yiaddr); // set the offered IP here
    u32ToBytes(m_nServerIP, pResponse->siaddr);
	// Build DHCP options
    // TODO: check if any other options is needed?
	unsigned nOptionsLen = BuildDHCPOptions (pResponse->options,
											 sizeof (pResponse->options),
                                             DHCP_OFFER, m_nServerIP,
                                             m_nLeaseTime);
#ifdef DEBUG
			debug_hexdump (pResponse->options, nOptionsLen, FromDHCPServer);
#endif
    return DHCP_FIXED_LEN + nOptionsLen; 
}

unsigned CDHCPServer::CraftDHCPAck (const DHCPHdr *pRequest, unsigned requestLen, DHCPHdr *pResponse)
{

  	// Find lease for this client
    CLogger *pLogger = CLogger::Get ();

	u32 nRequestedIP, nOfferedIP;
    CMACAddress nOfferedMAC, nRequestMAC (pRequest->chaddr);
    CString nOfferedMACStr, nRequestMACStr;
    boolean rc, is_ack = TRUE;

    nRequestMAC.Format (&nRequestMACStr);
   
    u8 RequestedIPBuf[4];
    if ((rc = GetDHCPOptionType(pRequest->options, requestLen - DHCP_FIXED_LEN,
                    DHCP_OPTION_LEASE_IP, 4, RequestedIPBuf)) == FALSE) {
        // unable to find field, instead got ciaddr
        my_memcpy(RequestedIPBuf, pRequest->ciaddr, 4);
    }
    nRequestedIP = BytesTou32(RequestedIPBuf);

    if (pLogger) {
        pLogger->Write (FromDHCPServer, LogNotice,
                "Mac address %s requests IP address %d.%d.%d.%d",
                (const char *)nRequestMACStr,
                (nRequestedIP >> 24) & 0xff, (nRequestedIP >> 16) & 0xff,
                (nRequestedIP >> 8) & 0xff, nRequestedIP & 0xff);
    }
    
    if ((rc = FindLease(pRequest->chaddr, nOfferedIP)) == TRUE) {
        if (nOfferedIP != nRequestedIP) {
            is_ack = FALSE;

            if (pLogger) {
                pLogger->Write (FromDHCPServer, LogNotice,
                        "MAC address %s already have active lease on IP address %d.%d.%d.%d, request NAKED",
                        (const char *)nRequestMACStr,
                        (nOfferedIP >> 24) & 0xff, (nOfferedIP >> 16) & 0xff,
                        (nOfferedIP >> 8) & 0xff, nOfferedIP & 0xff);
            }

            goto out; // no need further check, just reply NAK
        }

            if (pLogger) {
                pLogger->Write (FromDHCPServer, LogNotice,
                        "MAC address %s have recorded lease on requested IP address %d.%d.%d.%d, direct ACK",
                        (const char *)nRequestMACStr,
                        (nOfferedIP >> 24) & 0xff, (nOfferedIP >> 16) & 0xff,
                        (nOfferedIP >> 8) & 0xff, nOfferedIP & 0xff);
            }

        goto out;
    }
    
    u8 tempMACBuf[6];
    if ((rc = FindLease(nRequestedIP, tempMACBuf)) == TRUE) {
        nOfferedMAC = tempMACBuf;
        nOfferedMAC.Format (&nOfferedMACStr);

        if (nOfferedMAC != nRequestMAC) {
            is_ack = FALSE;
            if (pLogger) {
                pLogger->Write (FromDHCPServer, LogNotice,
                    "requests IP address %d.%d.%d.%d already assigned to Mac address %s, sending back NAK",
                    (nRequestedIP >> 24) & 0xff, (nRequestedIP >> 16) & 0xff,
                    (nRequestedIP >> 8) & 0xff, nRequestedIP & 0xff,
                    (const char *)nOfferedMACStr);
            }

            goto out; // no need further check, just reply NAK
        }
    }
		
    // No lease found - assign new one
	RecordLease (pRequest->chaddr, nRequestedIP);
    if (pLogger) {
	    pLogger->Write (FromDHCPServer, LogNotice,
					    "Lease recorded: %d.%d.%d.%d for MAC Address %s",
					    (nRequestedIP >> 24) & 0xff, (nRequestedIP >> 16) & 0xff,
                        (nRequestedIP >> 8) & 0xff, nRequestedIP & 0xff,
                        (const char *)nRequestMACStr);

    }

out:
	// Build DHCP response packet
    // TODO: check this chunk assignment
	pResponse->op = 2;			// BOOTREPLY
	pResponse->htype = pRequest->htype;
	pResponse->hlen = pRequest->hlen;
	pResponse->hops = 0;
	pResponse->xid = pRequest->xid;
	pResponse->secs = 0;
	pResponse->flags = pRequest->flags;
    pResponse->magic = BE32(DHCP_MAGIC);

	my_memset(pResponse->ciaddr, 0, 4);
    my_memcpy(pResponse->giaddr, pRequest->giaddr, 4);
    my_memcpy (pResponse->chaddr, pRequest->chaddr, 16);
    
    unsigned nOptionsLen;
    if (is_ack) {
        u32ToBytes(nRequestedIP, pResponse->yiaddr);
        u32ToBytes(m_nServerIP, pResponse->siaddr);
	    nOptionsLen = BuildDHCPOptions (pResponse->options,
                sizeof (pResponse->options), DHCP_ACKNOWLEDGE, 
                m_nServerIP, m_nLeaseTime);
    } 
    else {
        // empty both fields
        my_memset(pResponse->yiaddr, 0, 4);
        my_memset(pResponse->siaddr, 0, 4);
	    nOptionsLen = BuildDHCPOptions (pResponse->options,
                sizeof (pResponse->options), DHCP_NAK, 
                m_nServerIP, 0);
    }

#ifdef DEBUG
			debug_hexdump (pResponse->options, nOptionsLen, FromDHCPServer);
#endif

    return DHCP_FIXED_LEN + nOptionsLen;
}
