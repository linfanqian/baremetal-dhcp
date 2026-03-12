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
#include <pthread.h>

#include "dhcp-client.h"
#include "utils.h"
#include "workers-async.h"
#include "client-queue.h"
#include "xid-tab.h"

int sockfd = -1;
struct sockaddr_ll addr = {0};

int main() {
    srand(time(NULL));

    client_queue_init();
    xid_tab_init();

    sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sockfd < 0) { perror("socket"); return 1; }
 
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex = if_nametoindex("enp0s20f0u3c2");

    struct client_async clients[NUM_CLIENTS] = {0};
    int passed = 0, failed = 0, rc;
    long total_ms = 0, success_ms = 0, fail_ms = 0;

    for (unsigned i = 0; i < NUM_CLIENTS; i++) {
        random_mac(clients[i].c.mac);
        clients[i].c.xid = rand();
        clients[i].c.retries = 0;
        push_client(&clients[i]);
    }

    pthread_t workers[MAX_WORKERS];
    int worker_ids[MAX_WORKERS];
    for (unsigned i = 0; i < MAX_WORKERS; i++) {
        worker_ids[i] = i;
        pthread_create(&workers[i], NULL, worker_thread, &worker_ids[i]);
        usleep(10000);  // sleep for 10 ms between workers
    }

    pthread_t recv_manager;
    pthread_create(&recv_manager, NULL, recv_manager_thread, NULL);

    for (unsigned i = 0; i < MAX_WORKERS; i++)
        pthread_join(workers[i], NULL);
    pthread_cancel(recv_manager);

    for (unsigned i = 0; i < NUM_CLIENTS; i++) {
        if (clients[i].c.state == STATE_DONE) {
            success_ms += clients[i].c.total_ms;
            passed++;
        }
        else {
            fail_ms += clients[i].c.total_ms;
            failed++;
        }
        total_ms += clients[i].c.total_ms;
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
