//
// dhcpserver.cpp
//
// Minimal DHCP server implementation for Circle
//

#include "dhcpserver.h"
#include <circle/logger.h>
#include <circle/net/in.h>
#include <circle/string.h>
#include <circle/timer.h>
#include <circle/sched/scheduler.h>



#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_MAGIC 0x63825363

static const char FromDHCPServer[] = "dhcpserver";

// Custom memory functions to avoid include conflicts
static inline void *my_memcpy(void *dest, const void *src, unsigned int n)
{
	u8 *d = (u8 *)dest;
	const u8 *s = (const u8 *)src;
	for (unsigned int i = 0; i < n; i++)
		d[i] = s[i];
	return dest;
}

static inline void *my_memset(void *s, int c, unsigned int n)
{
	u8 *p = (u8 *)s;
	for (unsigned int i = 0; i < n; i++)
		p[i] = (u8)c;
	return s;
}

static inline int my_memcmp(const void *s1, const void *s2, unsigned int n)
{
	const u8 *a = (const u8 *)s1;
	const u8 *b = (const u8 *)s2;
	for (unsigned int i = 0; i < n; i++)
	{
		if (a[i] != b[i])
			return (int)a[i] - (int)b[i];
	}
	return 0;
}

CDHCPServer::CDHCPServer (CNetSubSystem *pNetSubSystem)
	: CTask (8192, "DHCPServer"),
	  m_pNetSubSystem (pNetSubSystem),
	  m_pSocket (0),
	  m_nServerIP (0),
	  m_nSubnetMask (0),
	  m_nGatewayIP (0),
	  m_nDHCPPoolStart (0),
	  m_nDHCPPoolEnd (0),
	  m_nLeaseTime (3600),	// 1 hour default
	  m_nNextLeaseIndex (0)
{
	assert (pNetSubSystem != 0);
	
	// Initialize leases
	for (unsigned i = 0; i < 10; i++)
	{
		m_aLeases[i].inUse = FALSE;
		m_aLeases[i].ipaddr = 0;
		m_aLeases[i].leaseStart = 0;
	}
	
	// Get configuration from network subsystem
	CNetConfig *pNetConfig = m_pNetSubSystem->GetConfig ();
	assert (pNetConfig != 0);
	
	// Get server IP - use operator to convert CIPAddress to u32
	const CIPAddress *pServerIP = pNetConfig->GetIPAddress ();
	m_nServerIP = (u32)*pServerIP;  // Convert to u32 through operator
	
	// Get subnet mask and gateway
	const u8 *pSubnetMask = pNetConfig->GetNetMask ();
	m_nSubnetMask = (pSubnetMask[0] << 24) | (pSubnetMask[1] << 16) | 
					(pSubnetMask[2] << 8) | pSubnetMask[3];
	
	const CIPAddress *pGateway = pNetConfig->GetDefaultGateway ();
	m_nGatewayIP = (u32)*pGateway;  // Convert to u32 through operator
	
	// Set up DHCP pool: assign addresses in range .100-.200
	// Example: if server is 192.168.1.1, pool is 192.168.1.100-192.168.1.200
	u32 nNetwork = m_nServerIP & m_nSubnetMask;
	m_nDHCPPoolStart = nNetwork | (100 << 0);
	m_nDHCPPoolEnd = nNetwork | (200 << 0);
	
	// Log configuration
	CLogger *pLogger = CLogger::Get ();
	if (pLogger)
	{
		CString IPStr;
		pServerIP->Format (&IPStr);
		pLogger->Write (FromDHCPServer, LogNotice, "DHCP Server configured at %s", 
						(const char *)IPStr);
	}
}

CDHCPServer::~CDHCPServer (void)
{
	if (m_pSocket)
	{
		delete m_pSocket;
		m_pSocket = 0;
	}
}

void CDHCPServer::Run (void)
{
	CLogger *pLogger = CLogger::Get ();
	
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

void CDHCPServer::ProcessDHCPPacket (const u8 *pPacket, unsigned nLength,
									  const CIPAddress &rClientIP, u16 nClientPort)
{
	CLogger *pLogger = CLogger::Get ();
	
	if (nLength < sizeof (DHCPPacket))
	{
		if (pLogger)
		{
			pLogger->Write (FromDHCPServer, LogWarning, "DHCP packet too small: %d bytes", nLength);
		}
		return;
	}
	
	const DHCPPacket *pDHCP = (const DHCPPacket *)pPacket;
	
	// Verify magic cookie
	u32 nMagic = (pDHCP->options[0] << 24) | (pDHCP->options[1] << 16) |
				 (pDHCP->options[2] << 8) | pDHCP->options[3];
	
	if (nMagic != DHCP_MAGIC)
	{
		if (pLogger)
		{
			pLogger->Write (FromDHCPServer, LogWarning, "Invalid DHCP magic cookie");
		}
		return;
	}
	
	// Get message type
	u8 nMessageType = GetDHCPOptionType (pDHCP->options + 4, sizeof (pDHCP->options) - 4,
										 DHCP_OPTION_MESSAGE_TYPE);
	
	if (pLogger)
	{
		const char *pTypeName = "";
		switch (nMessageType)
		{
			case DHCP_DISCOVER: pTypeName = "DISCOVER"; break;
			case DHCP_REQUEST:  pTypeName = "REQUEST"; break;
			case DHCP_OFFER:    pTypeName = "OFFER"; break;
			case DHCP_ACKNOWLEDGE: pTypeName = "ACK"; break;
		}
		(void) pTypeName; // Suppress unused variable warning
	}
	
	// Handle DHCP messages
	switch (nMessageType)
	{
		case DHCP_DISCOVER:
			SendDHCPOffer (pDHCP, rClientIP);
			break;
			
		case DHCP_REQUEST:
			SendDHCPAck (pDHCP, rClientIP);
			break;
			
		default:
			if (pLogger)
			{
				pLogger->Write (FromDHCPServer, LogWarning, "Unknown DHCP message type: %d",
							   nMessageType);
			}
			break;
	}
}

void CDHCPServer::SendDHCPOffer (const DHCPPacket *pRequest, const CIPAddress &rClientIP)
{
	CLogger *pLogger = CLogger::Get ();
	
	if (!m_pSocket)
		return;
	
	// Allocate IP address
	u32 nOfferedIP = AssignIP (pRequest->chaddr);
	
	// Record lease
	RecordLease (pRequest->chaddr, nOfferedIP);
	
	// Build DHCP response packet
	DHCPPacket *pResponse = new DHCPPacket;
	assert (pResponse != 0);
	
	my_memset (pResponse, 0, sizeof (DHCPPacket));
	
	pResponse->op = 2;			// BOOTREPLY
	pResponse->htype = pRequest->htype;
	pResponse->hlen = pRequest->hlen;
	pResponse->hops = 0;
	pResponse->xid = pRequest->xid;
	pResponse->secs = 0;
	pResponse->flags = pRequest->flags;
	pResponse->ciaddr = 0;
	pResponse->yiaddr = nOfferedIP;	// Your IP address
	pResponse->siaddr = m_nServerIP;	// Server IP
	pResponse->giaddr = pRequest->giaddr;
	
	my_memcpy (pResponse->chaddr, pRequest->chaddr, 16);
	
	// Set magic cookie
	pResponse->options[0] = 0x63;
	pResponse->options[1] = 0x82;
	pResponse->options[2] = 0x53;
	pResponse->options[3] = 0x63;
	
	// Build DHCP options
	unsigned nOptionsLen = BuildDHCPOptions (pResponse->options + 4,
											 sizeof (pResponse->options) - 4,
											 DHCP_OFFER, m_nServerIP, m_nLeaseTime);
	
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
}

void CDHCPServer::SendDHCPAck (const DHCPPacket *pRequest, const CIPAddress &rClientIP)
{
	CLogger *pLogger = CLogger::Get ();
	
	if (!m_pSocket)
		return;
	
	// Find lease for this client
	u32 nIP = 0;
	if (!FindLease (pRequest->chaddr, nIP))
	{
		// No lease found - assign new one
		nIP = AssignIP (pRequest->chaddr);
		RecordLease (pRequest->chaddr, nIP);
	}
	
	// Build DHCP response packet
	DHCPPacket *pResponse = new DHCPPacket;
	assert (pResponse != 0);
	
	my_memset (pResponse, 0, sizeof (DHCPPacket));
	
	pResponse->op = 2;			// BOOTREPLY
	pResponse->htype = pRequest->htype;
	pResponse->hlen = pRequest->hlen;
	pResponse->hops = 0;
	pResponse->xid = pRequest->xid;
	pResponse->secs = 0;
	pResponse->flags = pRequest->flags;
	pResponse->ciaddr = 0;
	pResponse->yiaddr = nIP;		// Your IP address
	pResponse->siaddr = m_nServerIP;	// Server IP
	pResponse->giaddr = pRequest->giaddr;
	
	my_memcpy (pResponse->chaddr, pRequest->chaddr, 16);
	
	// Set magic cookie
	pResponse->options[0] = 0x63;
	pResponse->options[1] = 0x82;
	pResponse->options[2] = 0x53;
	pResponse->options[3] = 0x63;
	
	// Build DHCP options
	unsigned nOptionsLen = BuildDHCPOptions (pResponse->options + 4,
											 sizeof (pResponse->options) - 4,
											 DHCP_ACKNOWLEDGE, m_nServerIP, m_nLeaseTime);
	
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
		CIPAddress LeaseIP (ipaddr);
		CString IPStr;
		LeaseIP.Format (&IPStr);
		
		pLogger->Write (FromDHCPServer, LogDebug,
					   "Lease recorded: %s for %02X:%02X:%02X:%02X:%02X:%02X",
					   (const char *)IPStr,
					   pHWAddr[0], pHWAddr[1], pHWAddr[2],
					   pHWAddr[3], pHWAddr[4], pHWAddr[5]);
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
