#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include "dhcp-client.h"
#include "utils.h"
#include "frame.h"

static int run_client(int sockfd, struct sockaddr_ll *addr, struct client *c) {

    unsigned char sbuf[1024];
    unsigned char rbuf[2048];
    
    struct build_meta bmeta;
    struct parse_meta pmeta;

    memset(&bmeta, 0x0, sizeof(bmeta));
    memcpy(bmeta.client_mac, c->mac, 6);
    memset(bmeta.requested_ip, 0x0, 4);
    bmeta.dhcp_type = DHCP_DISCOVER;
    bmeta.xid = c->xid;
    pmeta.xid = c->xid;
    int slen = build_frame(sbuf, sizeof(sbuf), bmeta);

#ifdef DEBUG
    printf("sending DHCP discover (%d bytes):\n", slen);
    debug_frame(sbuf, slen);
#endif

      if (sendto(sockfd, sbuf, slen, 0,
            (struct sockaddr*)addr, sizeof(*addr)) < 0) {
        perror("sendto");
        return -1;
    }

    c->state = STATE_DISCOVER;

    while (c->state != STATE_DONE && c->state != STATE_FAILED) {

        pmeta.xid = c->xid;
        int rlen = recv(sockfd, rbuf, sizeof(rbuf), 0);
        if (rlen < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if(++c->retries >= MAX_RETRIES) {
                    printf("client " MAC_FMT ": timeout, giving up\n",
                            MAC_ARG(c->mac));
                    c->state = STATE_FAILED;
                    return -1;
                }
                printf("client " MAC_FMT ": timeout, retrying (%d/%d)\n",
                        MAC_ARG(c->mac), c->retries, MAX_RETRIES);
                slen = build_frame(sbuf, sizeof(sbuf), bmeta);
                sendto(sockfd, sbuf, slen, 0, (struct sockaddr *)addr, sizeof(*addr));
                continue;
            }
            perror("recv");
            return -1;
        }

#ifdef DEBUG
        printf("\nReceived response (%d bytes): \n", rlen);
        debug_frame(rbuf, rlen);
#endif
        parse_frame(rbuf, rlen, &pmeta);
        switch (pmeta.dhcp_type) {
            case (DHCP_OFFER):
            {
#ifdef DEBUG
                printf("received DHCP OFFER packet with server id %d.%d.%d.%d:\n", 
                    pmeta.server_ip[0], pmeta.server_ip[1], pmeta.server_ip[2], pmeta.server_ip[3]);
                printf("offer ip %d.%d.%d.%d, subnet mask %d.%d.%d.%d\n",
                        pmeta.offered_ip[0], pmeta.offered_ip[1], pmeta.offered_ip[2], pmeta.offered_ip[3],
                        pmeta.subnet_mask[0], pmeta.subnet_mask[1], pmeta.subnet_mask[2], pmeta.subnet_mask[3]);
#endif
                // send request
                if (c->state != STATE_DISCOVER) continue; // ignore the packet, may be sent extra
                                                          //
                memcpy(bmeta.client_mac, c->mac, 6);
                bmeta.dhcp_type = DHCP_REQUEST;
                bmeta.xid = c->xid;
                memcpy(bmeta.requested_ip, pmeta.offered_ip, 4);
                memcpy(bmeta.server_ip, pmeta.server_ip, 4);
                slen = build_frame(sbuf, sizeof(sbuf), bmeta);
#ifdef DEBUG
                printf("sending request:\n");
                debug_frame(sbuf, slen);
#endif
                if (sendto(sockfd, sbuf, sizeof(sbuf), 0,
                    (struct sockaddr*)addr, sizeof(*addr)) < 0) {
                    perror("sendto");
                    return -1;
                }
                c->state = STATE_REQUEST;
                c->retries = 0;
            }
            break;
            case (DHCP_ACK): 
            {
#ifdef DEBUG
                printf("received DHCP ACK packet with server id %d.%d.%d.%d:\n", 
                    pmeta.server_ip[0], pmeta.server_ip[1], pmeta.server_ip[2], pmeta.server_ip[3]);
                printf("lease time %d\n", pmeta.lease_time);
#endif
                if (c->state != STATE_REQUEST) continue;

                memcpy(c->assigned_ip, pmeta.offered_ip, 4);
                printf("client " MAC_FMT ": assigned " IP_FMT "\n",
                        MAC_ARG(c->mac),
                        IP_ARG(c->assigned_ip));
                c->state = STATE_DONE;
            }
            break;
            case (DHCP_NAK):
            {
                printf("client " MAC_FMT ": got NAK, failing\n",
                        MAC_ARG(c->mac));
                c->state = STATE_FAILED;
            }
            break;
            default:
            break;
        }
    }
    return (c->state == STATE_DONE) ? 0 : -1;
}

static unsigned char client_mac[6] = {0x02, 0xde, 0xad, 0xbe, 0xef, 0x01};
static unsigned char server_mac[6] = {0xf0, 0xa7, 0x31, 0x61, 0x13, 0x50};

int main() {
    srand(time(NULL));
    int sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sockfd < 0) { perror("socket"); return 1; }

    struct timeval tv = { .tv_sec = TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
 
    struct sockaddr_ll addr = {0}; // what is this?
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex = if_nametoindex("enp0s20f0u3c2");
    
    struct client clients[NUM_CLIENTS] = {0};
    int passed = 0, failed = 0, rc;
    struct timespec start, end;
    long total_ms = 0, success_ms = 0, fail_ms = 0;

    for (int i = 0; i < NUM_CLIENTS; i++) {
        random_mac(clients[i].mac);
        clients[i].xid = rand();
        clients[i].retries = 0;

        printf(" --- client %d/%d --- \n", i+1, NUM_CLIENTS);
        clock_gettime(CLOCK_MONOTONIC, &start);
        rc = run_client(sockfd, &addr, &clients[i]);
        clock_gettime(CLOCK_MONOTONIC, &end);
        if (rc == 0) {
            passed++;
            success_ms += ((end.tv_sec - start.tv_sec) * 1000 
                    + (end.tv_nsec - start.tv_nsec) / 1000000); 
        } else {
            failed++;
            fail_ms += ((end.tv_sec - start.tv_sec) * 1000 
                    + (end.tv_nsec - start.tv_nsec) / 1000000); 
 
        }
        total_ms += ((end.tv_sec - start.tv_sec) * 1000 
                    + (end.tv_nsec - start.tv_nsec) / 1000000); 
    }
    
    printf("\n === results: %d/%d passed, %d/%d failed ===\n", 
            passed, NUM_CLIENTS, failed, NUM_CLIENTS);
    printf(" === average time usage: %ld ms (success %ld ms, fails %ld ms) === \n",
            total_ms / NUM_CLIENTS, 
            (passed == 0) ? 0 : success_ms / passed,
            (failed == 0) ? 0 : fail_ms / failed);
    close(sockfd);
    return 0;
}
