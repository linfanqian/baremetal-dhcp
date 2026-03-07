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
#include "frame.h"
#include "debug-frame.h"
#include "dhcpserver.h"
#include "mystring.h"
#include <circle/usb/usb.h>
#include <circle/netdevice.h>
#include <circle/string.h>
#include <circle/synchronize.h>
#include <circle/macros.h>
#include <circle/debug.h>

static const char FromKernel[] = "kernel";
static u8 ResponseFrameBuffer[sizeof(EthernetHdr) + sizeof(IPv4Hdr) + sizeof(UDPHdr) + sizeof(DHCPPacket)];


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

        my_memset(ResponseFrameBuffer, 0, sizeof(ResponseFrameBuffer));
        u8 *ResponseFramePtr = ResponseFrameBuffer;

        EthernetHdr *eth = (EthernetHdr *)FrameBuffer;
        logEthernetHdr(eth, &m_Logger);

        // do ethernet
        EthernetHdr *ethRes = (EthernetHdr *)ResponseFramePtr;
        // TODO: what if broadcast for dst?
        my_memcpy(ethRes->srcMAC, pEth0->GetMACAddress()->Get(), 6);
        my_memset(ethRes->dstMAC, 0xff, 6); // broadcasting for now
        ethRes->etherType = eth->etherType; // we only deal with 0x800 and potentially arp loll
        ResponseFramePtr += sizeof(EthernetHdr);
        
        if (eth->etherType == BE(0x800)) {
            IPv4Hdr *ipv4 = (IPv4Hdr *)(FrameBuffer + sizeof(EthernetHdr));
            logIPv4Hdr(ipv4, &m_Logger);

            // add ipv4 info
            IPv4Hdr *ipv4Res = (IPv4Hdr *)ResponseFramePtr;
            ipv4Res->versionIhl = 0x45;
            ipv4Res->tos = 0;
            ipv4Res->totalLen = BE(sizeof(IPv4Hdr) + sizeof(UDPHdr) + sizeof(DHCPPacket));
            ipv4Res->ttl = 64;
            ipv4Res->protocol = 17;
            ipv4Res->checksum = 0;

            // TODO: add the rest inside DHCP response?
            ResponseFramePtr += sizeof(IPv4Hdr);

            if (ipv4->protocol == 17) {
                UDPHdr *udp = (UDPHdr *)(FrameBuffer + sizeof(EthernetHdr) + sizeof(IPv4Hdr));
                logUDPHdr(udp, &m_Logger);

                UDPHdr *udpRes = (UDPHdr *)ResponseFramePtr;
                udpRes->srcPort = BE(67);
                udpRes->dstPort = BE(68);
                udpRes->totalLen = BE(sizeof(UDPHdr) + sizeof(DHCPPacket));
                udpRes->checksum = 0;
                ResponseFramePtr += sizeof(UDPHdr);

                if (BE(udp->dstPort) == 67) {
                    DHCPPacket *dhcp = (DHCPPacket *)(FrameBuffer + sizeof(EthernetHdr) + sizeof(IPv4Hdr) + sizeof(UDPHdr));
                    logDHCPHdr(dhcp, &m_Logger);
                    u8 msgType = serv.ProcessDHCPPacket(dhcp, BE(udp->totalLen) - sizeof(UDPHdr));
                    switch (msgType) {
                        case DHCP_DISCOVER:
                            {
                                DHCPPacket *dhcpRes = (DHCPPacket *)ResponseFramePtr;
                                serv.SendDHCPOffer(dhcp, 0, dhcpRes);
                                my_memcpy(ipv4Res->srcIP, dhcpRes->siaddr, 4);
                                my_memset(ipv4Res->dstIP, 0xff, 4);
                                ipv4Res->checksum = ipChecksum(ipv4Res);
                                m_Logger.Write(FromKernel, LogNotice, "Sending DHCP_OFFER in response to DISCOVER:");
                                logEthernetHdr(ethRes, &m_Logger);
                                logIPv4Hdr(ipv4Res, &m_Logger);
                                logUDPHdr(udpRes, &m_Logger);
                                logDHCPHdr(dhcpRes, &m_Logger);
                                pEth0->SendFrame(ResponseFrameBuffer, sizeof(ResponseFrameBuffer));
                            }
                            break;
                        case DHCP_REQUEST:
                            {
                                DHCPPacket *dhcpRes = (DHCPPacket *)ResponseFramePtr;
                                serv.SendDHCPAck(dhcp, 0, dhcpRes);
                                my_memcpy(ipv4Res->srcIP, dhcpRes->siaddr, 4);
                                my_memset(ipv4Res->dstIP, 0xff, 4);
                                ipv4Res->checksum = ipChecksum(ipv4Res);
                                m_Logger.Write(FromKernel, LogNotice, "Sending DHCP_ACK in response to REQUEST:");
                                logEthernetHdr(ethRes, &m_Logger);
                                logIPv4Hdr(ipv4Res, &m_Logger);
                                logUDPHdr(udpRes, &m_Logger);
                                logDHCPHdr(dhcpRes, &m_Logger);
                                pEth0->SendFrame(ResponseFrameBuffer, sizeof(ResponseFrameBuffer));
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
