#include "debug-frame.h"

#include <circle/macaddress.h>

static const char FromDebugFrame[] = "debugframe";

void logEthernetHdr(EthernetHdr *eth, CLogger *logger) {
    if (!logger)
        return;

    CMACAddress srcMACAddr = (CMACAddress) eth->srcMAC;
    CMACAddress dstMACAddr = (CMACAddress) eth->dstMAC;
    CString srcMACAddrStr, dstMACAddrStr;
    srcMACAddr.Format (&srcMACAddrStr);
    dstMACAddr.Format (&dstMACAddrStr);

    logger->Write(FromDebugFrame, LogNotice, "ETH: src %s | dst %s | type %u",
            (const char *)srcMACAddrStr, (const char *)dstMACAddrStr,
            BE(eth->etherType)
            );
}

void logIPv4Hdr(IPv4Hdr *ipv4, CLogger *logger) {
    if (!logger)
        return;

    logger->Write(FromDebugFrame, LogNotice, "IPv4: versionIhl %u | tos %u | len %u | ident %u",
            ipv4->versionIhl, ipv4->tos, BE(ipv4->totalLen), BE(ipv4->ident));
    logger->Write(FromDebugFrame, LogNotice, "IPv4: flags %u | ttl %u | protocol %u | checksum %u",
            BE(ipv4->flags), ipv4->ttl, ipv4->protocol, BE(ipv4->checksum));
    logger->Write(FromDebugFrame, LogNotice, "IPv4: src %d.%d.%d.%d | dst %d.%d.%d.%d",
            ipv4->srcIP[0], ipv4->srcIP[1], ipv4->srcIP[2], ipv4->srcIP[3],
            ipv4->dstIP[0], ipv4->dstIP[1], ipv4->dstIP[2], ipv4->dstIP[3]);
}

void logUDPHdr(UDPHdr *udp, CLogger *logger) {
    if (!logger)
        return;

    logger->Write(FromDebugFrame, LogNotice, "UDP: src port %u | dst port %u | len %u | checksum %u",
            BE(udp->srcPort), BE(udp->dstPort), BE(udp->totalLen), BE(udp->checksum));
}

void logDHCPHdr(DHCPHdr *dhcp, CLogger *logger) {
    if (!logger)
        return;

    logger->Write(FromDebugFrame, LogNotice, "DHCP: op %u | htype %u | hlen %u | hops %u | xid %x",
            dhcp->op, dhcp->htype, dhcp->hlen, dhcp->hops, BE32(dhcp->xid));

    logger->Write(FromDebugFrame, LogNotice, "DHCP: secs %u | flags %u",
            dhcp->secs, dhcp->flags);
    logger->Write(FromDebugFrame, LogNotice, "DHCP: ciaddr %d.%d.%d.%d | yiaddr %d.%d.%d.%d",
            dhcp->ciaddr[0], dhcp->ciaddr[1],
            dhcp->ciaddr[2], dhcp->ciaddr[3],
            dhcp->yiaddr[0], dhcp->yiaddr[1],
            dhcp->yiaddr[2], dhcp->yiaddr[3]);
    logger->Write(FromDebugFrame, LogNotice, "DHCP: siaddr %d.%d.%d.%d | giaddr %d.%d.%d.%d",
            dhcp->siaddr[0], dhcp->siaddr[1],
            dhcp->siaddr[2], dhcp->siaddr[3],
            dhcp->giaddr[0], dhcp->giaddr[1],
            dhcp->giaddr[2], dhcp->giaddr[3]);

    CMACAddress clientHdAddr = (CMACAddress) dhcp->chaddr;
    CString clientHdAddrStr;
    clientHdAddr.Format (&clientHdAddrStr);
    logger->Write(FromDebugFrame, LogNotice, "DHCP: chaddr %s | magic cookie %x",
            (const char *)clientHdAddrStr, BE32(dhcp->magic));
}
