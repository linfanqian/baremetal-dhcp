#include "mystring.h"
#include "dhcp.h"
#include <circle/timer.h>

static const char FromDHCPServer[] = "dhcpserver";

u32 CDHCPServer::AssignIP (const u8 *pHWAddr)
{
	(void) pHWAddr;  // Suppress unused parameter warning
	
	// Simple round-robin allocation
	static u32 nNextIP = 0;
    
	// ??? LOLLL REALLY
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
	
	// Find slot in lease table
	unsigned nSlot = m_nNextLeaseIndex;
	
	m_aLeases[nSlot].inUse = TRUE;
	m_aLeases[nSlot].ipaddr = ipaddr;
	m_aLeases[nSlot].leaseStart = CTimer::Get ()->GetClockTicks ();
	
	my_memcpy (m_aLeases[nSlot].hwaddr, pHWAddr, 6);
	
	m_nNextLeaseIndex = (m_nNextLeaseIndex + 1) % 10;
}

// find lease via MAC
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

// find lease via IP
boolean CDHCPServer::FindLease (const u32 ipaddr, u8 *pHWAddr) {
	for (unsigned i = 0; i < 10; i++)
	{
		if (m_aLeases[i].inUse && (m_aLeases[i].ipaddr == ipaddr))
		{
            my_memcpy(pHWAddr, m_aLeases[i].hwaddr, 6);
			return TRUE;
		}
	}
	return FALSE;
}


