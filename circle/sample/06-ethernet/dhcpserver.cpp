//
// dhcpserver.cpp
//
// Minimal DHCP server implementation for Circle
//

#include "dhcpserver.h"
#include "mystring.h"
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/timer.h>
#include <circle/sched/scheduler.h>
#include <circle/net/in.h>
#include <circle/debug.h>


#include <circle/macaddress.h>
#include <circle/net/ipaddress.h>
/*
 * net related modules commented out here
#include <circle/net/in.h>
*/

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
}

// let's just not do run for now
/*
void CDHCPServer::Run (void)
{
	CLogger *pLogger = CLogger::Get ();
    if (pLogger) 
    {
        pLogger->Write(FromDHCPServer, LogNotice, "called DHCP server run. unimplemented yet");
    }

	// Create UDP socket
	m_pSocket = new CSocket (m_pNetSubSystem, IPPROTO_UDP);
	assert (m_pSocket != 0);
	
	// Bind to DHCP server port
	if (m_pSocket->Bind (DHCP_SERVER_PORT) < 0)
	{
		if (pLogger)
		{
			pLogger->Write (FromDHCPServer, LogError, "Cannot bind to port %d", DHCP_SERVER_PORT);
		}
		return;
	}
	
	if (pLogger)
	{
		pLogger->Write (FromDHCPServer, LogNotice, "DHCP Server listening on port %d", 
						DHCP_SERVER_PORT);
	}
	
	// Main loop - receive and process DHCP packets
	u8 aBuffer[4096];
	CIPAddress ClientIP;
	u16 nClientPort;
	
	while (TRUE)
	{
		int nSize = m_pSocket->ReceiveFrom (aBuffer, sizeof(aBuffer), MSG_DONTWAIT, &ClientIP, &nClientPort);
		
		if (nSize > 0)
		{
			if (pLogger)
			{
				CString ClientIPStr;
				ClientIP.Format (&ClientIPStr);
				pLogger->Write (FromDHCPServer, LogDebug, 
							   "Received DHCP packet (%d bytes) from %s:%d",
							   nSize, (const char *)ClientIPStr, nClientPort);
			}
			
			ProcessDHCPPacket (aBuffer, nSize, ClientIP, nClientPort);
		}
		else if (nSize < 0)
		{
			// Timeout or error - continue
		}
		
		CScheduler::Get ()->Yield ();
	}
}
*/

u8 CDHCPServer::ProcessDHCPPacket (const DHCPPacket *pPacket, unsigned nLength)
{
	CLogger *pLogger = CLogger::Get ();
	
	if (nLength < DHCP_FIXED_LEN)
	{
		if (pLogger)
		{
			pLogger->Write (FromDHCPServer, LogWarning, "DHCP packet too small: expected %d, got %d bytes instead", DHCP_FIXED_LEN, nLength);
		}
		return 0;
	}
/*
#ifndef NDEBUG
			debug_hexdump (pPacket, nLength, FromDHCPServer);
#endif
*/
	const DHCPPacket *pDHCP = pPacket;
/*
    if (pLogger) {
        pLogger->Write (FromDHCPServer, LogNotice, "processing DHCP packet starting from %p, parsing options from %p",
                &pDHCP,
                &pDHCP->options);
    }
*/	
	// Verify magic cookie
    u32 magic_cookie = BE32(pDHCP->magic);
	if (magic_cookie != DHCP_MAGIC)
	{
		if (pLogger)
		{
			pLogger->Write (FromDHCPServer, LogWarning, "Invalid DHCP magic cookie: expected %x but instead got %x", DHCP_MAGIC, magic_cookie);
		}
		return 0;
	}
	
	// Get message type
	u8 nMessageType = GetDHCPOptionType (pDHCP->options, nLength - DHCP_FIXED_LEN,
										 DHCP_OPTION_MESSAGE_TYPE);
	
	if (pLogger)
	{
		CString pTypeName ("UNKNOWN");
		switch (nMessageType)
		{
			case DHCP_DISCOVER: pTypeName = "DISCOVER"; break;
			case DHCP_REQUEST:  pTypeName = "REQUEST"; break;
			case DHCP_OFFER:    pTypeName = "OFFER"; break;
			case DHCP_ACKNOWLEDGE: pTypeName = "ACK"; break;
		}
		// (void) pTypeName; // Suppress unused variable warning
        pLogger->Write(FromDHCPServer, LogNotice, "received DHCP %s packet", (const char *)pTypeName);
	}
	
    return nMessageType;
	// Handle DHCP messages
   /* 
	switch (nMessageType)
	{
		case DHCP_DISCOVER:
            {
			SendDHCPOffer (pDHCP, rClientIP);
            }
			break;
			
		case DHCP_REQUEST:
            {
			// SendDHCPAck (pDHCP, rClientIP);
            }
			break;
			
		default:
            {
			if (pLogger)
				pLogger->Write (FromDHCPServer, LogWarning, "Unknown DHCP message type: %d",
							   nMessageType);
            }
			break;
	}
    */
}

void CDHCPServer::SendDHCPOffer (const DHCPPacket *pRequest, const u32 &rClientIP,
        DHCPPacket *pResponse)
{
	CLogger *pLogger = CLogger::Get ();

    /*
	if (!m_pSocket)
		return;
	*/

	// Allocate IP address
	u32 nOfferedIP = AssignIP (pRequest->chaddr);
    if (pLogger) {
        pLogger->Write (FromDHCPServer, LogNotice, "Assigning IP address %d.%d.%d.%d",
                (nOfferedIP << 24) & 0xff, (nOfferedIP << 16) & 0xff,
                (nOfferedIP << 8) & 0xff, nOfferedIP & 0xff);
    }
	
	// Record lease
	RecordLease (pRequest->chaddr, nOfferedIP);
	
	// Build DHCP response packet
	// DHCPPacket *pResponse = new DHCPPacket;
	assert (pResponse != 0);
	
	my_memset (pResponse, 0, sizeof (DHCPPacket));
	
	pResponse->op = 2;			// BOOTREPLY
	pResponse->htype = pRequest->htype;
	pResponse->hlen = pRequest->hlen;
	pResponse->hops = 0;
	pResponse->xid = pRequest->xid;
	pResponse->secs = 0;
	pResponse->flags = pRequest->flags;
    my_memset(pResponse->ciaddr, 0, 4);
	my_memcpy(pResponse->giaddr, pRequest->giaddr, 4);

    /*
	pResponse->yiaddr = nOfferedIP;	    // Your IP address
	pResponse->siaddr = m_nServerIP;	// Server IP
    */
    u32ToBytes(nOfferedIP, pResponse->yiaddr);
    u32ToBytes(m_nServerIP, pResponse->siaddr);
	my_memcpy (pResponse->chaddr, pRequest->chaddr, 16);
	
	// Set magic cookie
    pResponse->magic = BE32(DHCP_MAGIC);
    /*
	pResponse->magic[0] = 0x63;
	pResponse->magic[1] = 0x82;
	pResponse->magic[2] = 0x53;
	pResponse->magic[3] = 0x63;
	*/
	// Build DHCP options
	unsigned nOptionsLen = BuildDHCPOptions (pResponse->options,
											 sizeof (pResponse->options),
											 DHCP_OFFER, m_nServerIP, m_nLeaseTime);
/*
#ifndef NDEBUG
			debug_hexdump (pResponse, sizeof (DHCPPacket), FromDHCPServer);
#endif
	// Send response to broadcast address (255.255.255.255:68)
	CIPAddress BroadcastIP (0xFFFFFFFF);
	
	m_pSocket->SendTo (pResponse, sizeof (DHCPPacket) - sizeof (pResponse->options) + nOptionsLen + 4,
					   MSG_DONTWAIT, BroadcastIP, DHCP_CLIENT_PORT);
	
	if (pLogger)
	{
		CIPAddress OfferedIP (nOfferedIP);
		CString IPStr;
		OfferedIP.Format (&IPStr);
		
		pLogger->Write (FromDHCPServer, LogNotice,
					   "DHCP OFFER: IP %s sent (XID: 0x%X)",
					   (const char *)IPStr, pRequest->xid);
	}
	
	delete pResponse;
    */
}

void CDHCPServer::SendDHCPAck (const DHCPPacket *pRequest, const u32 &rClientIP,
        DHCPPacket *pResponse)
{
	CLogger *pLogger = CLogger::Get ();

    /*	
	if (!m_pSocket)
		return;
	*/
	// Find lease for this client
	u32 nIP = 0;
	if (!FindLease (pRequest->chaddr, nIP))
	{
		// No lease found - assign new one
		nIP = AssignIP (pRequest->chaddr);
		RecordLease (pRequest->chaddr, nIP);
	}
	
	// Build DHCP response packet
	// DHCPPacket *pResponse = new DHCPPacket;
	assert (pResponse != 0);
	
	my_memset (pResponse, 0, sizeof (DHCPPacket));
	
	pResponse->op = 2;			// BOOTREPLY
	pResponse->htype = pRequest->htype;
	pResponse->hlen = pRequest->hlen;
	pResponse->hops = 0;
	pResponse->xid = pRequest->xid;
	pResponse->secs = 0;
	pResponse->flags = pRequest->flags;
	my_memset(pResponse->ciaddr, 0, 4);
    my_memcpy(pResponse->giaddr, pRequest->giaddr, 4);
    /*
	pResponse->yiaddr = nIP;		// Your IP address
	pResponse->siaddr = m_nServerIP;	// Server IP
	*/
    u32ToBytes(nIP, pResponse->yiaddr);
    u32ToBytes(m_nServerIP, pResponse->siaddr);

	my_memcpy (pResponse->chaddr, pRequest->chaddr, 16);
	
	// Set magic cookie
    pResponse->magic = BE32(DHCP_MAGIC);
    /*
	pResponse->options[0] = 0x63;
	pResponse->options[1] = 0x82;
	pResponse->options[2] = 0x53;
	pResponse->options[3] = 0x63;
	*/
	// Build DHCP options
	unsigned nOptionsLen = BuildDHCPOptions (pResponse->options,
											 sizeof (pResponse->options),
											 DHCP_ACKNOWLEDGE, m_nServerIP, m_nLeaseTime);
	
    /*
	// Send response to broadcast address (255.255.255.255:68)
	CIPAddress BroadcastIP (0xFFFFFFFF);
	
	m_pSocket->SendTo (pResponse, sizeof (DHCPPacket) - sizeof (pResponse->options) + nOptionsLen + 4,
					   MSG_DONTWAIT, BroadcastIP, DHCP_CLIENT_PORT);
	
	if (pLogger)
	{
		CIPAddress AckIP (nIP);
		CString IPStr;
		AckIP.Format (&IPStr);
		
		pLogger->Write (FromDHCPServer, LogNotice,
					   "DHCP ACK: IP %s confirmed (XID: 0x%X)",
					   (const char *)IPStr, pRequest->xid);
	}
	
	delete pResponse;
    */
}

u32 CDHCPServer::AssignIP (const u8 *pHWAddr)
{
	(void) pHWAddr;  // Suppress unused parameter warning
	
	// Simple round-robin allocation
	static u32 nNextIP = 0;
	
	if (nNextIP == 0)
	{
		nNextIP = m_nDHCPPoolStart;
	}
	
	u32 nIP = nNextIP;
	nNextIP++;
	
	if (nNextIP > m_nDHCPPoolEnd)
	{
		nNextIP = m_nDHCPPoolStart;
	}
	
	return nIP;
}

void CDHCPServer::RecordLease (const u8 *pHWAddr, u32 ipaddr)
{
	CLogger *pLogger = CLogger::Get ();
	
	// Find slot in lease table
	unsigned nSlot = m_nNextLeaseIndex;
	
	m_aLeases[nSlot].inUse = TRUE;
	m_aLeases[nSlot].ipaddr = ipaddr;
	m_aLeases[nSlot].leaseStart = CTimer::Get ()->GetClockTicks ();
	
	my_memcpy (m_aLeases[nSlot].hwaddr, pHWAddr, 6);
	
	m_nNextLeaseIndex = (m_nNextLeaseIndex + 1) % 10;
	
	if (pLogger)
	{
		
		pLogger->Write (FromDHCPServer, LogDebug,
					   "Lease recorded: %d.%d.%d.%d for some MAC",
					   (ipaddr >> 24) & 0xff, (ipaddr >> 16) & 0xff,
                       (ipaddr >> 8) & 0xff, ipaddr & 0xff);
	}
}

boolean CDHCPServer::FindLease (const u8 *pHWAddr, u32 &ipaddr)
{
	for (unsigned i = 0; i < 10; i++)
	{
		if (m_aLeases[i].inUse && my_memcmp (m_aLeases[i].hwaddr, pHWAddr, 6) == 0)
		{
			ipaddr = m_aLeases[i].ipaddr;
			return TRUE;
		}
	}
	
	return FALSE;
}

u8 CDHCPServer::GetDHCPOptionType (const u8 *pOptions, unsigned nLength, u8 nType)
{
	for (unsigned i = 0; i < nLength; )
	{
		u8 nOptType = pOptions[i++];
		
		if (nOptType == DHCP_OPTION_END)
			break;
		
		if (i >= nLength)
			break;
		
		u8 nOptLen = pOptions[i++];
		
		if (nOptType == nType && i < nLength)
		{
			return pOptions[i];
		}
		
		i += nOptLen;
	}
	
	return 0;
}

boolean CDHCPServer::GetDHCPOption (const u8 *pOptions, unsigned nLength,
									 u8 nType, u8 *pBuffer, unsigned nBufLen)
{
	for (unsigned i = 0; i < nLength; )
	{
		u8 nOptType = pOptions[i++];
		
		if (nOptType == DHCP_OPTION_END)
			break;
		
		if (i >= nLength)
			break;
		
		u8 nOptLen = pOptions[i++];
		
		if (nOptType == nType)
		{
			unsigned nCopyLen = (nOptLen < nBufLen) ? nOptLen : nBufLen;
			my_memcpy (pBuffer, pOptions + i, nCopyLen);
			return TRUE;
		}
		
		i += nOptLen;
	}
	
	return FALSE;
}

unsigned CDHCPServer::BuildDHCPOptions (u8 *pBuffer, unsigned nBufLen,
										u8 nMessageType, u32 nServerID, u32 nLeaseTime)
{
	unsigned nOffset = 0;
	
	// Message Type
	if (nOffset + 3 <= nBufLen)
	{
		pBuffer[nOffset++] = DHCP_OPTION_MESSAGE_TYPE;
		pBuffer[nOffset++] = 1;
		pBuffer[nOffset++] = nMessageType;
	}
	
	// DHCP Server Identifier
	if (nOffset + 6 <= nBufLen)
	{
		pBuffer[nOffset++] = DHCP_OPTION_DHCP_SERVER_ID;
		pBuffer[nOffset++] = 4;
		pBuffer[nOffset++] = (nServerID >> 24) & 0xFF;
		pBuffer[nOffset++] = (nServerID >> 16) & 0xFF;
		pBuffer[nOffset++] = (nServerID >> 8) & 0xFF;
		pBuffer[nOffset++] = nServerID & 0xFF;
	}
	
	// Subnet Mask
	if (nOffset + 6 <= nBufLen)
	{
		pBuffer[nOffset++] = DHCP_OPTION_SUBNET_MASK;
		pBuffer[nOffset++] = 4;
		pBuffer[nOffset++] = (m_nSubnetMask >> 24) & 0xFF;
		pBuffer[nOffset++] = (m_nSubnetMask >> 16) & 0xFF;
		pBuffer[nOffset++] = (m_nSubnetMask >> 8) & 0xFF;
		pBuffer[nOffset++] = m_nSubnetMask & 0xFF;
	}
	
	// Router/Gateway
	if (nOffset + 6 <= nBufLen)
	{
		pBuffer[nOffset++] = DHCP_OPTION_ROUTER;
		pBuffer[nOffset++] = 4;
		pBuffer[nOffset++] = (m_nGatewayIP >> 24) & 0xFF;
		pBuffer[nOffset++] = (m_nGatewayIP >> 16) & 0xFF;
		pBuffer[nOffset++] = (m_nGatewayIP >> 8) & 0xFF;
		pBuffer[nOffset++] = m_nGatewayIP & 0xFF;
	}
	
	// Lease Time
	if (nOffset + 6 <= nBufLen)
	{
		pBuffer[nOffset++] = DHCP_OPTION_LEASE_TIME;
		pBuffer[nOffset++] = 4;
		pBuffer[nOffset++] = (nLeaseTime >> 24) & 0xFF;
		pBuffer[nOffset++] = (nLeaseTime >> 16) & 0xFF;
		pBuffer[nOffset++] = (nLeaseTime >> 8) & 0xFF;
		pBuffer[nOffset++] = nLeaseTime & 0xFF;
	}
	
	// End option
	if (nOffset < nBufLen)
	{
		pBuffer[nOffset++] = DHCP_OPTION_END;
	}
	
	return nOffset;
}
