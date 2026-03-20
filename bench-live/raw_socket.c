#include <stdio.h>
#include <string.h>
#include <pcap.h>

#include "raw_socket.h"

static pcap_t *handle = NULL;

int raw_socket_init(const char *iface) {
    char errbuf[PCAP_ERRBUF_SIZE];

    // Use create/activate to enable immediate mode (no buffering delay)
    handle = pcap_create(iface, errbuf);
    if (!handle) {
        fprintf(stderr, "pcap_create(%s): %s\n", iface, errbuf);
        return -1;
    }
    pcap_set_snaplen(handle, 65535);
    pcap_set_promisc(handle, 1);
    pcap_set_immediate_mode(handle, 1);  // deliver packets immediately, no buffering
    pcap_set_timeout(handle, 1);         // 1ms fallback timeout

    if (pcap_activate(handle) != 0) {
        fprintf(stderr, "pcap_activate(%s): %s\n", iface, pcap_geterr(handle));
        pcap_close(handle);
        handle = NULL;
        return -1;
    }

    // Filter to DHCP traffic only to avoid drowning in unrelated frames
    struct bpf_program fp;
    const char *filter = "udp port 67 or udp port 68";
    if (pcap_compile(handle, &fp, filter, 0, PCAP_NETMASK_UNKNOWN) == 0) {
        pcap_setfilter(handle, &fp);
        pcap_freecode(&fp);
    }

    return 0;
}

int raw_send(const unsigned char *buf, int len) {
    return pcap_inject(handle, buf, len);
}

int raw_recv(unsigned char *buf, int maxlen) {
    struct pcap_pkthdr *header;
    const u_char *data;

    int rc = pcap_next_ex(handle, &header, &data);
    if (rc != 1)   // 0 = timeout, -1 = error, -2 = breakloop
        return -1;

    int copy_len = (int)header->caplen < maxlen ? (int)header->caplen : maxlen;
    memcpy(buf, data, copy_len);
    return copy_len;
}

void raw_socket_close(void) {
    if (handle) {
        pcap_breakloop(handle);
        pcap_close(handle);
        handle = NULL;
    }
}
