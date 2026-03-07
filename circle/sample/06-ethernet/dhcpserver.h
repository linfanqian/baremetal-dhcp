//
// dhcpserver.h
//
// Minimal DHCP server implementation for Circle
// Handles DHCP DISCOVER, REQUEST, and ACKNOWLEDGE
//

#ifndef _dhcpserver_h
#define _dhcpserver_h

// #include <circle/sched/task.h>
// commented for now cause may include 
// defining stuff that requires dynamic IP initialization for now

// #include <circle/net/netsubsystem.h>
// #include <circle/net/socket.h>
// #include <circle/net/ipaddress.h>
#include <circle/netdevice.h>
#include <circle/types.h>
// DHCP packet structure (simplified)
#ifdef __GNUC__
#define PACKED __attribute__ ((packed))
#else
#define PACKED
#endif

struct DHCPPacket
{
	u8  op;			// 1=request, 2=reply
	u8  htype;		// hardware type (1=Ethernet)
	u8  hlen;		// hardware length
	u8  hops;
	u32 xid;		// transaction ID
	u16 secs;
	u16 flags;
	u8 ciaddr[4];		// client IP
	u8 yiaddr[4];		// your IP (offered)
	u8 siaddr[4];		// server IP
	u8 giaddr[4];		// gateway IP
	u8  chaddr[16];	// client hardware address
	u8  sname[64];	// server name
	u8  file[128];	// boot file
	u32 magic;		// magic cookie (0x63825363)
	u8  options[312];	// options
} PACKED;

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACKNOWLEDGE 5

#define DHCP_OPTION_SUBNET_MASK			1
#define DHCP_OPTION_ROUTER				3
#define DHCP_OPTION_LEASE_TIME			51
#define DHCP_OPTION_DHCP_SERVER_ID		54
#define DHCP_OPTION_MESSAGE_TYPE		53
#define DHCP_OPTION_PARAM_REQUEST_LIST	55
#define DHCP_OPTION_END		     		255

#define BE32(x) (((u32)(x) >> 24) & 0xFF) | (((u32)(x) >> 8) & 0xFF00) | \
                (((u32)(x) << 8) & 0xFF0000) | (((u32)(x) << 24) & 0xFF000000)

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
	CDHCPServer (CNetDevice *pNetDevice);
	~CDHCPServer (void);

	// void Run (void);

	u8 ProcessDHCPPacket (const DHCPPacket *pPacket, unsigned nLength);
	void SendDHCPOffer (const DHCPPacket *pRequest, const u32 &rClientIP,
            DHCPPacket *pResponse);
	void SendDHCPAck (const DHCPPacket *pRequest, const u32 &rClientIP,
            DHCPPacket *pResponse);

private:
	u32 AssignIP (const u8 *pHWAddr);
	void RecordLease (const u8 *pHWAddr, u32 ipaddr);
	boolean FindLease (const u8 *pHWAddr, u32 &ipaddr);
	
	u8 GetDHCPOptionType (const u8 *pOptions, unsigned nLength, u8 nType);
	boolean GetDHCPOption (const u8 *pOptions, unsigned nLength, 
						   u8 nType, u8 *pBuffer, unsigned nBufLen);
	unsigned BuildDHCPOptions (u8 *pBuffer, unsigned nBufLen, u8 nMessageType, 
							   u32 nServerID, u32 nLeaseTime);
    
    /* -> not having these for now
	CNetSubSystem  *m_pNetSubSystem;
	CSocket		   *m_pSocket;
	*/

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
