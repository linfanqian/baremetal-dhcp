#include <string.h>

#include "dhcp-client.h"
#include "lib/cqueue.h"

static cq_t cq;

void worker_thread(void *arg) {

    int sockfd = *(int *)arg;
    struct sockaddr_ll *addr; // TODO: global var

    while (1) {
        // main loop: work on one client
        uint64_t c_async_ptr;
        cq_pop64(&cq, &c_async_ptr);
        struct client_async *c_async = (struct client_async *)c_async_ptr;

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

    };
}


