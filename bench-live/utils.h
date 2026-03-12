#ifndef __UTILS_H__
#define __UTILS_H__

#define MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ARG(m) (m)[0],(m)[1],(m)[2],(m)[3],(m)[4],(m)[5]

#define IP_FMT "%d.%d.%d.%d"
#define IP_ARG(i) (i)[0],(i)[1],(i)[2],(i)[3]

void random_mac(unsigned char *mac);
void hexdump(const unsigned char *buf, int len);
#endif
