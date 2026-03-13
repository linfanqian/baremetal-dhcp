#ifndef __DHCP_CLIENT_H__
#define __DHCP_CLIENT_H__

#define CLIENT_ASYNC

#include <stdint.h>

#ifdef CLIENT_ASYNC
    #include <pthread.h>
#endif

#define MAX_WORKERS 32
#define NUM_CLIENTS 2048
#define TIMEOUT_SEC 1   // every client timeout after 1 second (seems appropriate based on average latency)
#define MAX_RETRIES 3

enum Client_State {
    STATE_DISCOVER = 1,
    STATE_REQUEST,
    STATE_DONE,
    STATE_FAILED
};


struct client {
    uint8_t mac[6];
    uint32_t xid;
    enum Client_State state;
    int retries;
    uint8_t offered_ip[4];
    uint8_t server_ip[4];
    uint8_t assigned_ip[4];
    struct timespec start;
    long total_ms;
};

#ifdef CLIENT_ASYNC
struct mailbox {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    uint8_t buf[2048];
    int buf_len;
};

struct client_async {
    struct client c;
    
    // for worker dispatch
    int worker_id;
    // for recv manager to transfer data to worker id
    struct mailbox mb;
    };
#endif


#endif
