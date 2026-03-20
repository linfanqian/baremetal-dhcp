#ifndef __RAW_SOCKET_H__
#define __RAW_SOCKET_H__

// Platform-agnostic raw socket abstraction using libpcap.
// On Linux: requires AF_PACKET support (root). On macOS: uses BPF via pcap (root).

int  raw_socket_init(const char *iface);
int  raw_send(const unsigned char *buf, int len);
int  raw_recv(unsigned char *buf, int maxlen);
void raw_socket_close(void);

#endif
