#include "mystring.h"
#include "dhcp.h"

boolean CDHCPServer::GetDHCPOptionType (const u8 *pOptions, unsigned nLength, u8 nType,
        unsigned fieldLength, u8 *buf)
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
            // found the field, but unable to satisfy the condition
            if (nOptLen < fieldLength || i + nOptLen > nLength)
                return FALSE;

            my_memcpy(buf, pOptions + i, fieldLength);
			return TRUE;
		}
		
		i += nOptLen;
	}
	
	return FALSE;
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
                               u8 nMessageType, u32 nServerID,
                               u32 nLeaseTime)
{
    /*
     * following RFC 2132 recommended order:
     * option 53  - Message Type 
     * option 54  - Server Identifier
     * option 51  - Lease Time
     * option 58  - Renewal Time (T1)  (optional) -> not built here
     * option 59  - Rebinding Time (T2)(optional) -> not built here
     * option 1   - Subnet Mask
     * option 3   - Router/Gateway
     * option 6   - DNS Servers        (optional)
     * 255        - End
     */

	unsigned nOffset = 0;

	// Message Type
	if (nOffset + 3 <= nBufLen)
	{
		pBuffer[nOffset++] = DHCP_OPTION_MESSAGE_TYPE;
		pBuffer[nOffset++] = 1; // 1 byte
		pBuffer[nOffset++] = nMessageType;
	}
	
	// DHCP Server Identifier
	if (nOffset + 6 <= nBufLen)
	{
		pBuffer[nOffset++] = DHCP_OPTION_DHCP_SERVER_ID;
		pBuffer[nOffset++] = 4; // 4 bytes
		pBuffer[nOffset++] = (nServerID >> 24) & 0xFF;
		pBuffer[nOffset++] = (nServerID >> 16) & 0xFF;
		pBuffer[nOffset++] = (nServerID >> 8) & 0xFF;
		pBuffer[nOffset++] = nServerID & 0xFF;
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
		pBuffer[nOffset++] = DHCP_OPTION_GATEWAY;
		pBuffer[nOffset++] = 4;
		pBuffer[nOffset++] = (m_nGatewayIP >> 24) & 0xFF;
		pBuffer[nOffset++] = (m_nGatewayIP >> 16) & 0xFF;
		pBuffer[nOffset++] = (m_nGatewayIP >> 8) & 0xFF;
		pBuffer[nOffset++] = m_nGatewayIP & 0xFF;
	}
	
    // End option
	if (nOffset < nBufLen)
	{
		pBuffer[nOffset++] = DHCP_OPTION_END;
	}
	
	return nOffset;
}
