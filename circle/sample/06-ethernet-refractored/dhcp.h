//
// dhcpserver.h
//
// Minimal DHCP server implementation for Circle
// Handles DHCP DISCOVER, REQUEST, and ACKNOWLEDGE
//

#ifndef _dhcp_h
#define _dhcp_h

/*
#include <circle/sched/task.h>
// commented for now cause may include 
// defining stuff that requires dynamic IP initialization for now

#include <circle/net/netsubsystem.h>
#include <circle/net/socket.h>
#include <circle/net/ipaddress.h>
*/
#include "headers.h"
#include <circle/netdevice.h>
#include <circle/types.h>

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACKNOWLEDGE 5
#define DHCP_NAK         6

#define DHCP_OPTION_SUBNET_MASK			1
#define DHCP_OPTION_GATEWAY		        3
#define DHCP_OPTION_LEASE_IP            50         
#define DHCP_OPTION_LEASE_TIME			51
#define DHCP_OPTION_MESSAGE_TYPE		53
#define DHCP_OPTION_DHCP_SERVER_ID		54
#define DHCP_OPTION_PARAM_REQUEST_LIST	55
#define DHCP_OPTION_END		     		255

// Lease entry structure
struct DHCPLease
{
	u8   hwaddr[6];		// MAC address
	u32  ipaddr;		// Assigned IP address
	u32  leaseStart;	// Lease start time (in ticks)
	boolean inUse;		// Is this lease slot in use?
};

class CDHCPServer
// removing this for now
// : CTask
{
public:
    // inside dhcp-server.cpp
	CDHCPServer (CNetDevice *pNetDevice);
	~CDHCPServer (void);

	// void Run (void);

	u8 ProcessDHCPHdr (const DHCPHdr *pDHCP, unsigned nLength);
	unsigned CraftDHCPOffer (const DHCPHdr *pRequest, DHCPHdr *pResponse);
	unsigned CraftDHCPAck (const DHCPHdr *pRequest, unsigned requestLen, DHCPHdr *pResponse);

private:
    // lease related helper functions (inside dhcp-leases.cpp)
	u32 AssignIP (const u8 *pHWAddr);
	void RecordLease (const u8 *pHWAddr, u32 ipaddr);
	boolean FindLease (const u8 *pHWAddr, u32 &ipaddr);
    boolean FindLease (const u32 ipaddr, u8 *pHWAddr); 
	
    // option related help functions (inside dhcp-options.cpp)
	// u8 GetDHCPOptionType (const u8 *pOptions, unsigned nLength, u8 nType);
    boolean GetDHCPOptionType (const u8 *pOptions, unsigned nLength, u8 nType,
            unsigned fieldLength, u8 *buf);
	boolean GetDHCPOption (const u8 *pOptions, unsigned nLength, 
						   u8 nType, u8 *pBuffer, unsigned nBufLen);
	unsigned BuildDHCPOptions (u8 *pBuffer, unsigned nBufLen, 
                               u8 nMessageType, u32 nServerID, u32 nLeaseTime);

    // DHCP configuration
    CNetDevice *m_pNetDevice;
	u32 m_nServerIP;		// This server's IP
	u32 m_nSubnetMask;
	u32 m_nGatewayIP;
	u32 m_nDHCPPoolStart;	// First IP to assign
	u32 m_nDHCPPoolEnd;		// Last IP to assign
	u32 m_nLeaseTime;		// Lease time in seconds
                        
	// Lease information (simplified - just track last assigned IP)
	DHCPLease m_aLeases[10];	// Pool of 10 leases
	unsigned m_nNextLeaseIndex;
};

#endif
