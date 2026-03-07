//
// kernel.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2014-2024  R. Stange <rsta2@o2online.de>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "kernel.h"
#include "headers.h"
#include "dhcp.h"
#include "debug-frame.h"
#include "mystring.h"
#include <circle/usb/usb.h>
#include <circle/netdevice.h>
#include <circle/string.h>
#include <circle/synchronize.h>
#include <circle/macros.h>
#include <circle/debug.h>

static const char FromKernel[] = "kernel";
// TODO: currently using the same statically allocated buffer for msg sending, 
// may be problematic if we are going to do any async stuff later
static u8 ResponseFrameBuffer[sizeof(EthernetHdr) + sizeof(IPv4Hdr) + sizeof(UDPHdr) + sizeof(DHCPHdr)];

static u16 ipChecksum(IPv4Hdr *ip)
{
    u32 sum = 0;
    u16 *ptr = (u16 *)ip;
    for (unsigned i = 0; i < sizeof(IPv4Hdr) / 2; i++)
        sum += ptr[i];
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return ~(u16)sum;
}

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer)
#if RASPPI <= 3
	, m_USBHCI (&m_Interrupt, &m_Timer)
#endif
{
	m_ActLED.Blink (5);	// show we are alive
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
	{
		bOK = m_Screen.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
	}

	if (bOK)
	{
		CDevice *pTarget = m_DeviceNameService.GetDevice (m_Options.GetLogDevice (), FALSE);
		if (pTarget == 0)
		{
			pTarget = &m_Screen;
		}
        // thank you we don't have screen, just use UART
        pTarget = &m_Serial;

		bOK = m_Logger.Initialize (pTarget);
	}

	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	if (bOK)
	{
#if RASPPI <= 3
		bOK = m_USBHCI.Initialize ();
#elif RASPPI == 4
		bOK = m_Bcm54213.Initialize ();
#else
		bOK = m_MACB.Initialize ();
#endif
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);

	CNetDevice *pEth0 = CNetDevice::GetNetDevice (0);
	if (pEth0 == 0)
	{
		m_Logger.Write (FromKernel, LogError, "Net device not found");

		return ShutdownHalt;
	}

	while (   !pEth0->IsLinkUp ()
	       && pEth0->UpdatePHY ())
	{
		m_Logger.Write (FromKernel, LogNotice, "Waiting for Ethernet PHY to come up");

		m_Timer.MsDelay (2000);
	}

    CDHCPServer serv = CDHCPServer(pEth0);

	m_Logger.Write (FromKernel, LogNotice, "Dumping received broadcasts");

	while (1)
	{
		DMA_BUFFER (u8, FrameBuffer, FRAME_BUFFER_SIZE);
		unsigned nFrameLength;

		if (pEth0->ReceiveFrame (FrameBuffer, &nFrameLength))
		{

        // clears the buffer for new response packets
        my_memset(ResponseFrameBuffer, 0, sizeof(ResponseFrameBuffer));
        u8 *ResponseFramePtr = ResponseFrameBuffer;

        EthernetHdr *eth = (EthernetHdr *)FrameBuffer;
#ifdef INPUTDEBUG
        logEthernetHdr(eth, &m_Logger);
#endif
        // do ethernet
        EthernetHdr *ethRes = (EthernetHdr *)ResponseFramePtr;
        // TODO: I am not sure? currently we assume that for all packets (only OFFER and ACK)
        // we will broadcast (i.e. dstMAC = FF::FF::FF::FF::FF),
        // and srcMAC is our MAC address
        my_memcpy(ethRes->srcMAC, pEth0->GetMACAddress()->Get(), 6);
        my_memset(ethRes->dstMAC, 0xff, 6); // broadcasting for now
    
        // TODO: we assume that the response packets will be the same
        // as the request packet (should be valid assumption for ARP IP ICMP??)
        ethRes->etherType = eth->etherType; // we only deal with 0x800 and potentially arp loll
        ResponseFramePtr += sizeof(EthernetHdr);
        
        if (eth->etherType == BE(0x800)) {
            IPv4Hdr *ipv4 = (IPv4Hdr *)(FrameBuffer + sizeof(EthernetHdr));
#ifdef INPUTDEBUG
            logIPv4Hdr(ipv4, &m_Logger);
#endif

            // TODO: go over this gigantic assignment
            // add ipv4 info
            IPv4Hdr *ipv4Res = (IPv4Hdr *)ResponseFramePtr;
            ipv4Res->versionIhl = 0x45;
            ipv4Res->tos = 0;
            ipv4Res->totalLen = BE(sizeof(IPv4Hdr) + sizeof(UDPHdr) + sizeof(DHCPHdr));
            ipv4Res->ttl = 64;
            ipv4Res->protocol = 17;
            ipv4Res->checksum = 0;

            ResponseFramePtr += sizeof(IPv4Hdr);

            if (ipv4->protocol == 17) {
                UDPHdr *udp = (UDPHdr *)(FrameBuffer + sizeof(EthernetHdr) + sizeof(IPv4Hdr));
#ifdef INPUTDEBUG
                logUDPHdr(udp, &m_Logger);
#endif

                // TODO: go over this gigantic assignment
                UDPHdr *udpRes = (UDPHdr *)ResponseFramePtr;
                udpRes->srcPort = BE(67);
                udpRes->dstPort = BE(68);
                udpRes->totalLen = BE(sizeof(UDPHdr) + sizeof(DHCPHdr));
                udpRes->checksum = 0; // checksum is not calculated immediately cause the IP address may need assignment after DHCP process
                ResponseFramePtr += sizeof(UDPHdr);

                if (BE(udp->dstPort) == 67) {
                    DHCPHdr *dhcp = (DHCPHdr *)(FrameBuffer + sizeof(EthernetHdr) + sizeof(IPv4Hdr) + sizeof(UDPHdr));
#ifdef INPUTDEBUG
                    logDHCPHdr(dhcp, &m_Logger);
#endif

                    // TODO: the current design incorporates Di's initial architecture
                    // basically we have a "ProcessDHCPHdr that does some basic checking
                    // 1. magic cookie verification
                    // 2. important option search (now just 53 for DHCP type)
                    // and return message type for further processing
                    unsigned DHCPRequestLen = BE(udp->totalLen) - sizeof(UDPHdr);
                    u8 msgType = serv.ProcessDHCPHdr(dhcp, DHCPRequestLen);
                    switch (msgType) {
                        case DHCP_DISCOVER:
                            {
                                DHCPHdr *dhcpRes = (DHCPHdr *)ResponseFramePtr;
                                unsigned DHCPLen = serv.CraftDHCPOffer(dhcp, dhcpRes);
                                if (DHCPLen > 0) { 
                                    // len of 0 means something is wrong, don't send packet
                                    my_memcpy(ipv4Res->srcIP, dhcpRes->siaddr, 4); // here we set source IP as our server IP 
                                    my_memset(ipv4Res->dstIP, 0xff, 4); // here we set destination IP as 255.255.255.255
                                    ipv4Res->checksum = ipChecksum(ipv4Res); // compute checksum for IP, it seems that checksum for UDP is optional so left at 0
#ifdef OUTPUTDEBUG
                                    m_Logger.Write(FromKernel, LogNotice, "Sending DHCP_OFFER in response to DISCOVER:");
                                    logEthernetHdr(ethRes, &m_Logger);
                                    logIPv4Hdr(ipv4Res, &m_Logger);
                                    logUDPHdr(udpRes, &m_Logger);
                                    logDHCPHdr(dhcpRes, &m_Logger);
#endif

                                    // sending the frame
                                    // TODO: currently we don't have UDP socket structure etc.
                                    // we are basically communicating frames
                                    pEth0->SendFrame(ResponseFrameBuffer, sizeof(ResponseFrameBuffer));
                                }
                            }
                            break;
                        case DHCP_REQUEST:
                            {
                                DHCPHdr *dhcpRes = (DHCPHdr *)ResponseFramePtr;
                                // TODO: technically we need to add logic for NACK
                                unsigned DHCPLen = serv.CraftDHCPAck(dhcp, DHCPRequestLen, dhcpRes);
                                if (DHCPLen > 0) {
                                    my_memcpy(ipv4Res->srcIP, dhcpRes->siaddr, 4); // basically same as above
                                    my_memset(ipv4Res->dstIP, 0xff, 4); // TODO: not sure if dstIP can be the assigned IP here 
                                    ipv4Res->checksum = ipChecksum(ipv4Res);
#ifdef OUTPUTDEBUG
                                    m_Logger.Write(FromKernel, LogNotice, "Sending DHCP_ACK in response to REQUEST:");
                                    logEthernetHdr(ethRes, &m_Logger);
                                    logIPv4Hdr(ipv4Res, &m_Logger);
                                    logUDPHdr(udpRes, &m_Logger);
                                    logDHCPHdr(dhcpRes, &m_Logger);
#endif
                                    pEth0->SendFrame(ResponseFrameBuffer, sizeof(ResponseFrameBuffer));
                                }
                            }
                            break;
                        default:
                            break;
                    }
                }
            }
        }
		
    }
}
	return ShutdownHalt;
}
