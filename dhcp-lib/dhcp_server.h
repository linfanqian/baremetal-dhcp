#ifndef DHCP_SERVER_H
#define DHCP_SERVER_H

/*
 * Compile-time lease mode selection via:
 *   -DDHCP_LEASE_MODE_TABLE      (default in circle sample) per-MAC lease table
 *   -DDHCP_LEASE_MODE_BMVAR      bitmap pool, variable lease time
 *   -DDHCP_LEASE_MODE_BMUNI      bitmap pool, unified lease time
 *   -DDHCP_LEASE_MODE_HASHMAP    hashmap pool, unified lease time
 */

#include <stdint.h>

#if defined(DHCP_LEASE_MODE_TABLE)
#include "dhcp_table.h"
#elif defined(DHCP_LEASE_MODE_BMVAR)
#include "dhcp_bitmap_vartime.h"
#elif defined(DHCP_LEASE_MODE_BMUNI)
#include "dhcp_bitmap_unitime.h"
#elif defined(DHCP_LEASE_MODE_NPRC)
#include "dhcp_nprc.h"
#elif defined(DHCP_LEASE_MODE_HASHMAP)  
#include "dhcp_hashmap.h"
#endif

/* DHCP Message Types */
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_DECLINE  4
#define DHCP_ACK      5
#define DHCP_NAK      6

/* DHCP Option Codes */
#define DHCP_OPT_PAD                    0
#define DHCP_OPT_SUBNET_MASK            1
#define DHCP_OPT_ROUTER                 3
#define DHCP_OPT_DNS_SERVER             6
#define DHCP_OPT_HOSTNAME               12
#define DHCP_OPT_REQUESTED_IP           50
#define DHCP_OPT_LEASE_TIME             51
#define DHCP_OPT_MESSAGE_TYPE           53
#define DHCP_OPT_SERVER_ID              54
#define DHCP_OPT_PARAM_REQUEST_LIST     55
#define DHCP_OPT_MAX_MESSAGE_SIZE       57
#define DHCP_OPT_END                    255

/* DHCP Magic Cookie (host byte order) */
#define DHCP_MAGIC_COOKIE 0x63825363

/* UDP Port Numbers */
#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

/*
 * DHCP Message Structure (in-memory, host-byte-order IPs).
 * Not packed — used purely as a working buffer, never cast directly
 * onto a raw ethernet frame.  Use the caller's adapter to convert
 * between this and whatever wire-format struct the upper layer uses.
 */
typedef struct {
    uint8_t op;                 /* Message type: 1 = BOOTREQUEST, 2 = BOOTREPLY */
    uint8_t htype;              /* Hardware address type (1 = Ethernet) */
    uint8_t hlen;               /* Hardware address length */
    uint8_t hops;               /* Hop count */
    uint32_t xid;               /* Transaction ID */
    uint16_t secs;              /* Elapsed time in seconds */
    uint16_t flags;             /* Flags */
    uint32_t ciaddr;            /* Client IP address (host order) */
    uint32_t yiaddr;            /* Your (client) IP address (host order) */
    uint32_t siaddr;            /* Server IP address (host order) */
    uint32_t giaddr;            /* Gateway IP address (host order) */
    uint8_t chaddr[16];         /* Client hardware address */
    uint8_t sname[64];          /* Server hostname (optional) */
    uint8_t file[128];          /* Boot filename (optional) */
    uint32_t magic_cookie;      /* Magic cookie: 0x63825363 (host order) */
    uint8_t options[308];       /* Optional parameters field */
} dhcp_message_t;

/* DHCP Server Configuration */
typedef struct {
    uint32_t server_ip;         /* Server IP address (host order) */
    uint32_t subnet_mask;       /* Subnet mask (host order) */
    uint32_t gateway_ip;        /* Gateway/Router IP (host order) */
    uint32_t dns_ip;            /* DNS server IP (host order) */
    uint32_t pool_start;        /* First IP in pool (host order) */
    uint32_t pool_end;          /* Last IP in pool (host order) */
    uint32_t lease_time;        /* Default lease time in seconds */
} dhcp_config_t;

/* DHCP Server State */
typedef struct {
    dhcp_config_t config;
#if defined(DHCP_LEASE_MODE_TABLE)
    dhcp_tablepool_t pool;
#elif defined(DHCP_LEASE_MODE_BMVAR)
    dhcp_bmpool_var_t pool;
#elif defined(DHCP_LEASE_MODE_BMUNI)
    dhcp_bmpool_uni_t pool;
#elif defined(DHCP_LEASE_MODE_NPRC)
    dhcp_nprcpool_t pool;
#elif defined(DHCP_LEASE_MODE_HASHMAP)  
    dhcp_hashpool_t pool;
#endif
} dhcp_server_t;

/* Shared message utilities */
uint8_t dhcp_get_message_type(dhcp_message_t *msg);
void dhcp_set_message_type(dhcp_message_t *msg, uint8_t type);
void dhcp_build_offer(dhcp_server_t *server, dhcp_message_t *request, dhcp_message_t *offer, uint32_t offered_ip);
void dhcp_build_ack(dhcp_server_t *server, dhcp_message_t *request, dhcp_message_t *ack, uint32_t assigned_ip);
void dhcp_build_nak(dhcp_message_t *request, dhcp_message_t *nak);
void dhcp_add_option(dhcp_message_t *msg, uint8_t option, uint8_t length, uint8_t *data);
uint8_t *dhcp_get_option(dhcp_message_t *msg, uint8_t option, uint8_t *length);

/* Helper functions */
void uint32_to_ip(uint32_t ip, uint8_t *a, uint8_t *b, uint8_t *c, uint8_t *d);
uint32_t ip_to_uint32(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

#if defined(DHCP_LEASE_MODE_TABLE)
void dhcp_init_server_table(dhcp_server_t *server, dhcp_config_t *config,
                            dhcp_lease_t *leases, uint16_t max_leases);
void dhcp_process_message_table(dhcp_server_t *server, dhcp_message_t *request, dhcp_message_t *response, uint32_t cur_time);
#elif defined(DHCP_LEASE_MODE_BMVAR)
void dhcp_init_server_bmvar(dhcp_server_t *server, dhcp_config_t *config);
void dhcp_process_message_bmvar(dhcp_server_t *server, dhcp_message_t *request, dhcp_message_t *response, uint32_t cur_time);
#elif defined(DHCP_LEASE_MODE_BMUNI)
void dhcp_init_server_bmuni(dhcp_server_t *server, dhcp_config_t *config);
void dhcp_process_message_bmuni(dhcp_server_t *server, dhcp_message_t *request, dhcp_message_t *response, uint32_t cur_time);
#endif

#if defined(DHCP_LEASE_MODE_NPRC)
void dhcp_init_server_nprc(dhcp_server_t *server, 
        dhcp_config_t *config);
void dhcp_process_message_nprc(dhcp_server_t *server, 
        dhcp_message_t *request, dhcp_message_t *response);
#endif


#if defined(DHCP_LEASE_MODE_HASHMAP)
void dhcp_init_server_hashmap(dhcp_server_t *server, dhcp_config_t *config);
void dhcp_process_message_hashmap(dhcp_server_t *server, dhcp_message_t *request, dhcp_message_t *response, uint32_t cur_time);
#endif
#endif /* DHCP_SERVER_H */
