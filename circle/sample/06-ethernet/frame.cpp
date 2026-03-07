#include "frame.h"

CString processSrc(const u8 *pFrame, unsigned nLen) 
{
	CString Sender ("???");

    if (nLen >= 14)
	    {
		    CMACAddress MACSender (pFrame+6);
			MACSender.Format (&Sender);
		}
    return Sender;
}

CString processData(const u8 *pFrame, unsigned nLen, CDHCPServer *serv) {

	CString Protocol ("???");

    if (nLen >= 14)
	{
	    unsigned nProtocol = *(unsigned short *) (pFrame+12);
		switch (nProtocol)
		{
		    case BE (0x800):
                {
                    Protocol = "IP";
                    if (nLen >= 34) {
                        u8 nIPProtocol = pFrame[23];
                        if (nIPProtocol == 17) {
                            // udp
                            Protocol += " UDP";

                            if (nLen >= 42) {

                                unsigned nDstPort = *(unsigned short *)(pFrame + 36);

                                if (nDstPort == BE(67)) { 
                                    Protocol += " DHCP";
                                }
                            }
                        }
                    }
                }
				break;

			case BE (0x806):
				Protocol = "ARP";
				break;

			default:
				break;
		}
	}
    
    return Protocol;
}
