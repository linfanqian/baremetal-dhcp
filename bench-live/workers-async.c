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
#include "utils.h"

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
        
        printf("WORKER#%d: received client with MAC %x:%x:%x:%x:%x:%x and xid %u\n",
                worker_id,
                curr_c->c.mac[0], curr_c->c.mac[1], curr_c->c.mac[2], 
                curr_c->c.mac[3], curr_c->c.mac[4], curr_c->c.mac[5],
                curr_c->c.xid);

        // 1. insert into the hash table so observable bt recv manager
        curr_c->worker_id = worker_id;
        // random hack: before inserted into xid table it is inaccessible to 
        // the recv manager, so i don't need lock here?
        curr_c->mb.buf_len = 0;
        xid_tab_insert(curr_c); 
        
#ifdef DEBUG_HASH
        // 1.5. verify insertion
        struct client_async *stored_c = xid_tab_lookup(curr_c->c.xid);
        if (stored_c != curr_c) {
            printf("WORKER#%d ERROR: expected to return address at 0x%lx, instead got 0x%lx\n",
                    worker_id, (uint64_t) curr_c, (uint64_t) stored_c);

            clock_gettime(CLOCK_MONOTONIC, &end);
            curr_c->c.total_ms = ((end.tv_sec - start.tv_sec) * 1000 
                    + (end.tv_nsec - start.tv_nsec) / 1000000); 
            curr_c->c.state = STATE_FAILED; 
            continue;
        }
        else {
            printf("WORKER#%d SUCCESS: found client at 0x%lx in the hash table\n", worker_id, (uint64_t) stored_c);
        }
#endif

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

#ifdef DEBUG_DHCP
        printf("sending DHCP discover (%d bytes):\n", slen);
        debug_frame(sbuf, slen);
#endif

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
                        printf("client " MAC_FMT ": timeout, giving up\n",
                                MAC_ARG(curr_c->c.mac));
                        curr_c->c.state = STATE_FAILED;
                        goto out;
                    }
                    printf("client " MAC_FMT ": timeout, retrying (%d/%d)\n",
                        MAC_ARG(curr_c->c.mac), curr_c->c.retries, MAX_RETRIES);
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
#ifdef DEBUG_DHCP
        printf("\nReceived response (%d bytes): \n", rlen);
        debug_frame(rbuf, rlen);
#endif

        parse_frame(rbuf, rlen, &pmeta);
        switch (pmeta.dhcp_type) {
            case (DHCP_OFFER):
            {
#ifdef DEBUG_DHCP
                printf("received DHCP OFFER packet with server id %d.%d.%d.%d:\n", 
                    pmeta.server_ip[0], pmeta.server_ip[1], pmeta.server_ip[2], pmeta.server_ip[3]);
                printf("offer ip %d.%d.%d.%d, subnet mask %d.%d.%d.%d\n",
                        pmeta.offered_ip[0], pmeta.offered_ip[1], pmeta.offered_ip[2], pmeta.offered_ip[3],
                        pmeta.subnet_mask[0], pmeta.subnet_mask[1], pmeta.subnet_mask[2], pmeta.subnet_mask[3]);
#endif
                // send request
                if (curr_c->c.state != STATE_DISCOVER) continue; // ignore the packet, may be sent extra
                                                          //
                memcpy(bmeta.client_mac, curr_c->c.mac, 6);
                bmeta.dhcp_type = DHCP_REQUEST;
                bmeta.xid = curr_c->c.xid;
                memcpy(bmeta.requested_ip, pmeta.offered_ip, 4);
                memcpy(bmeta.server_ip, pmeta.server_ip, 4);
                slen = build_frame(sbuf, sizeof(sbuf), bmeta);
#ifdef DEBUG_DHCP
                printf("sending request:\n");
                debug_frame(sbuf, slen);
#endif
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
#ifdef DEBUG_DHCP
                printf("received DHCP ACK packet with server id %d.%d.%d.%d:\n", 
                    pmeta.server_ip[0], pmeta.server_ip[1], pmeta.server_ip[2], pmeta.server_ip[3]);
                printf("lease time %d\n", pmeta.lease_time);
#endif
                if (curr_c->c.state != STATE_REQUEST) continue;

                memcpy(curr_c->c.assigned_ip, pmeta.offered_ip, 4);
                printf("client " MAC_FMT ": assigned " IP_FMT "\n",
                        MAC_ARG(curr_c->c.mac),
                        IP_ARG(curr_c->c.assigned_ip));
                curr_c->c.state = STATE_DONE;
            }
            break;
            case (DHCP_NAK):
            {
                printf("client " MAC_FMT ": got NAK, failing\n",
                        MAC_ARG(curr_c->c.mac));
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

#ifdef DEBUG_HASH
        // 3.5. verify removal
        stored_c = xid_tab_lookup(curr_c->c.xid); 
        if (stored_c) {
            printf("WORKER#%d ERROR: expected to remove instance with xid %u and ptr 0x%lx, still found it in hash table\n", worker_id, curr_c->c.xid, (uint64_t) stored_c);

            clock_gettime(CLOCK_MONOTONIC, &end);
            curr_c->c.total_ms = ((end.tv_sec - start.tv_sec) * 1000 
                    + (end.tv_nsec - start.tv_nsec) / 1000000); 
            curr_c->c.state = STATE_FAILED; 
 
            continue;
        }
        else {
            printf("WORKER#%d SUCCESS: removed instance and cannot find it in the hash table\n", worker_id);
        }
#endif
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
