#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <errno.h>

#include "workers-async.h"
#include "client-queue.h"
#include "xid-tab.h"
#include "frame.h"

extern int sockfd;
extern struct sockaddr_ll addr;

void *worker_thread(void *arg) {

    int worker_id = *(int *)arg;

    struct client_async *curr_c = NULL;
    struct timespec start, end, timeout;

   while (1) {
        // main loop: work on one client
        curr_c = pop_client();

        if (!curr_c)
            return NULL;
#ifdef DEBUG 
        printf("WORKER#%d: received client with MAC %x:%x:%x:%x:%x:%x and xid %u\n",
                worker_id,
                curr_c->c.mac[0], curr_c->c.mac[1], curr_c->c.mac[2], 
                curr_c->c.mac[3], curr_c->c.mac[4], curr_c->c.mac[5],
                curr_c->c.xid);
#endif

        // 1. insert into the hash table so observable bt recv manager
        curr_c->worker_id = worker_id;
        // random hack: before inserted into xid table it is inaccessible to 
        // the recv manager, so i don't need lock here?
        curr_c->mb.buf_len = 0;
        xid_tab_insert(curr_c); 

        clock_gettime(CLOCK_MONOTONIC, &start);
        // 2. do networking!!!!
        
        unsigned char sbuf[1024];
        unsigned char rbuf[1024];
    
        struct build_meta bmeta;
        struct parse_meta pmeta;

        memset(&bmeta, 0x0, sizeof(bmeta));
        memcpy(bmeta.client_mac, curr_c->c.mac, 6);
        memset(bmeta.requested_ip, 0x0, 4);
        bmeta.dhcp_type = DHCP_DISCOVER;
        bmeta.xid = curr_c->c.xid;
        pmeta.xid = curr_c->c.xid;
        int slen = build_frame(sbuf, sizeof(sbuf), bmeta);
        int rlen;

        if (sendto(sockfd, sbuf, slen, 0,
            (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("sendto");
            goto out;
        }

        curr_c->c.state = STATE_DISCOVER;

        while (curr_c->c.state != STATE_DONE && curr_c->c.state != STATE_FAILED) {

            pmeta.xid = curr_c->c.xid;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += TIMEOUT_SEC;
            
            pthread_mutex_lock(&curr_c->mb.lock);
            while (!curr_c->mb.buf_len) {
                int rc = pthread_cond_timedwait(&curr_c->mb.cond,
                        &curr_c->mb.lock,
                        &timeout);

                if (rc == ETIMEDOUT) {
                    pthread_mutex_unlock(&curr_c->mb.lock);
                    if(++curr_c->c.retries >= MAX_RETRIES) {
#ifdef DEBUG
                        printf("client " MAC_FMT ": timeout, giving up\n",
                                MAC_ARG(curr_c->c.mac));
#endif
                        curr_c->c.state = STATE_FAILED;
                        goto out;
                    }
#ifdef DEBUG
                    printf("client " MAC_FMT ": timeout, retrying (%d/%d)\n",
                        MAC_ARG(curr_c->c.mac), curr_c->c.retries, MAX_RETRIES);
#endif
                    slen = build_frame(sbuf, sizeof(sbuf), bmeta);
                    sendto(sockfd, sbuf, slen, 0, (struct sockaddr *)&addr, sizeof(addr));
                
                    clock_gettime(CLOCK_REALTIME, &timeout);
                    timeout.tv_sec += TIMEOUT_SEC;
                    pthread_mutex_lock(&curr_c->mb.lock);
                    continue;
            }
        }
        memcpy(rbuf, curr_c->mb.buf, curr_c->mb.buf_len);
        rlen = curr_c->mb.buf_len;
        // clear mailbox
        curr_c->mb.buf_len = 0;
        pthread_mutex_unlock(&curr_c->mb.lock);

        parse_frame(rbuf, rlen, &pmeta);
        switch (pmeta.dhcp_type) {
            case (DHCP_OFFER):
            {
                // send request
                if (curr_c->c.state != STATE_DISCOVER) continue; // ignore the packet, may be sent extra
                                                          //
                memcpy(bmeta.client_mac, curr_c->c.mac, 6);
                bmeta.dhcp_type = DHCP_REQUEST;
                bmeta.xid = curr_c->c.xid;
                memcpy(bmeta.requested_ip, pmeta.offered_ip, 4);
                memcpy(bmeta.server_ip, pmeta.server_ip, 4);
                slen = build_frame(sbuf, sizeof(sbuf), bmeta);

                if (sendto(sockfd, sbuf, slen, 0,
                    (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                    perror("sendto");
                    goto out;
                }
                curr_c->c.state = STATE_REQUEST;
                curr_c->c.retries = 0;
            }
            break;
            case (DHCP_ACK): 
            {
                if (curr_c->c.state != STATE_REQUEST) continue;
                memcpy(curr_c->c.assigned_ip, pmeta.offered_ip, 4);
                curr_c->c.state = STATE_DONE;
            }
            break;
            case (DHCP_NAK):
            {
                curr_c->c.state = STATE_FAILED;
            }
            break;
            default:
            break;
        }
    }

out:
        clock_gettime(CLOCK_MONOTONIC, &end);

        curr_c->c.total_ms = ((end.tv_sec - start.tv_sec) * 1000 
                    + (end.tv_nsec - start.tv_nsec) / 1000000); 

        // 3. finish: remove the instance and confirm removal
        xid_tab_remove(curr_c->c.xid);
   }
}

void *recv_manager_thread(void *arg) {

    (void)arg;
    
    struct client_async *recv_c;
    unsigned char rbuf[1024];
    int rlen, rc;
    uint32_t xid;

    while (1) {

        rlen = recv(sockfd, rbuf, sizeof(rbuf), 0);
        if (rlen < 0) continue;

        rc = parse_xid(rbuf, rlen, &xid); 
        if (!rc) continue;

        recv_c = xid_tab_lookup(xid);
        if (!recv_c) continue;

        pthread_mutex_lock(&recv_c->mb.lock);
        memcpy(recv_c->mb.buf, rbuf, rlen);
        recv_c->mb.buf_len = rlen;

        pthread_cond_signal(&recv_c->mb.cond);
        pthread_mutex_unlock(&recv_c->mb.lock);

    }
    return NULL;
}
