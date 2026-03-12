#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "utils.h"

void random_mac(uint8_t *mac) {
    for (int i = 0; i < 6; i++)
        mac[i] = rand() & 0xff;
    mac[0] &= 0xfe; // clear multicast bit
    mac[0] |= 0x02; // set locally administered bit
}

void hexdump(const unsigned char *buf, int len) {
    for (int i = 0; i < len; i++) {
        if (i % 16 == 0) printf("\n%04x: ", i);
        printf("%02x ", buf[i]);
    }
    printf("\n");
}

