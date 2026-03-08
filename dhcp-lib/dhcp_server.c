#include "dhcp_server.h"
#include "dhcp_compat.h"

/* Helper function to convert integer to IP address bytes */
void uint32_to_ip(uint32_t ip, uint8_t *a, uint8_t *b, uint8_t *c, uint8_t *d) {
    *a = (ip >> 24) & 0xFF;
    *b = (ip >> 16) & 0xFF;
    *c = (ip >> 8) & 0xFF;
    *d = ip & 0xFF;
}

uint32_t ip_to_uint32(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d;
}

/* Get the DHCP message type from options */
uint8_t dhcp_get_message_type(dhcp_message_t *msg) {
    uint8_t length = 0;
    uint8_t *opt = dhcp_get_option(msg, DHCP_OPT_MESSAGE_TYPE, &length);

    if (opt && length >= 1)
        return opt[0];

    return 0;
}

/* Set the DHCP message type in options */
void dhcp_set_message_type(dhcp_message_t *msg, uint8_t type) {
    dhcp_add_option(msg, DHCP_OPT_MESSAGE_TYPE, 1, &type);
}

/* Add an option to DHCP message */
void dhcp_add_option(dhcp_message_t *msg, uint8_t option, uint8_t length, uint8_t *data) {
    uint8_t *ptr = msg->options;

    /* If options appear uninitialized (all zero), set an explicit END marker
       so the scan below can find the end quickly and new options can be
       appended at the start of the options field. */
    if (ptr[0] == 0) {
        ptr[0] = DHCP_OPT_END;
    }

    /* Find end of options */
    while (ptr < (msg->options + 308) && *ptr != DHCP_OPT_END) {
        if (*ptr == DHCP_OPT_PAD) {
            ptr++;
            continue;
        }
        ptr += 2 + ptr[1]; /* Skip option code and length, then data */
    }

    /* Add new option if space available */
    if (ptr + 2 + length + 1 <= msg->options + 308) {
        *ptr++ = option;
        *ptr++ = length;
        if (data && length > 0) {
            kmemcpy(ptr, data, length);
        }
        ptr += length;
        if (ptr < msg->options + 308) {
            *ptr = DHCP_OPT_END;
        }
    }
}

/* Get an option from DHCP message */
uint8_t *dhcp_get_option(dhcp_message_t *msg, uint8_t option, uint8_t *length) {
    uint8_t *ptr = msg->options;

    while (*ptr != DHCP_OPT_END && ptr < (msg->options + 308)) {
        if (*ptr == option) {
            *length = ptr[1];
            return &ptr[2];
        }

        if (*ptr == DHCP_OPT_PAD) {
            ptr++;
            continue;
        }

        ptr += 2 + ptr[1];
    }

    *length = 0;
    return (uint8_t *)0;
}

/* Build a DHCP OFFER message */
void dhcp_build_offer(dhcp_server_t *server, dhcp_message_t *request,
                      dhcp_message_t *offer, uint32_t offered_ip) {
    kmemset(offer, 0, sizeof(dhcp_message_t));

    offer->op = 2;              /* BOOTREPLY */
    offer->htype = request->htype;
    offer->hlen = request->hlen;
    offer->hops = 0;
    offer->xid = request->xid;
    offer->secs = 0;
    offer->flags = request->flags;
    offer->ciaddr = 0;
    offer->yiaddr = offered_ip;
    offer->siaddr = server->config.server_ip;
    offer->giaddr = request->giaddr;
    kmemcpy(offer->chaddr, request->chaddr, 16);
    offer->magic_cookie = DHCP_MAGIC_COOKIE;

    dhcp_set_message_type(offer, DHCP_OFFER);

    uint8_t server_id_bytes[4];
    uint32_to_ip(server->config.server_ip, &server_id_bytes[0], &server_id_bytes[1],
                 &server_id_bytes[2], &server_id_bytes[3]);
    dhcp_add_option(offer, DHCP_OPT_SERVER_ID, 4, server_id_bytes);

    uint8_t subnet_bytes[4];
    uint32_to_ip(server->config.subnet_mask, &subnet_bytes[0], &subnet_bytes[1],
                 &subnet_bytes[2], &subnet_bytes[3]);
    dhcp_add_option(offer, DHCP_OPT_SUBNET_MASK, 4, subnet_bytes);

    uint8_t router_bytes[4];
    uint32_to_ip(server->config.gateway_ip, &router_bytes[0], &router_bytes[1],
                 &router_bytes[2], &router_bytes[3]);
    dhcp_add_option(offer, DHCP_OPT_ROUTER, 4, router_bytes);

    uint8_t dns_bytes[4];
    uint32_to_ip(server->config.dns_ip, &dns_bytes[0], &dns_bytes[1],
                 &dns_bytes[2], &dns_bytes[3]);
    dhcp_add_option(offer, DHCP_OPT_DNS_SERVER, 4, dns_bytes);

    uint8_t lease_bytes[4];
    uint32_t lease_time = server->config.lease_time;
    lease_bytes[0] = (lease_time >> 24) & 0xFF;
    lease_bytes[1] = (lease_time >> 16) & 0xFF;
    lease_bytes[2] = (lease_time >> 8) & 0xFF;
    lease_bytes[3] = lease_time & 0xFF;
    dhcp_add_option(offer, DHCP_OPT_LEASE_TIME, 4, lease_bytes);
}

/* Build a DHCP ACK message */
void dhcp_build_ack(dhcp_server_t *server, dhcp_message_t *request,
                    dhcp_message_t *ack, uint32_t assigned_ip) {
    kmemset(ack, 0, sizeof(dhcp_message_t));

    ack->op = 2;                /* BOOTREPLY */
    ack->htype = request->htype;
    ack->hlen = request->hlen;
    ack->hops = 0;
    ack->xid = request->xid;
    ack->secs = 0;
    ack->flags = request->flags;
    ack->ciaddr = request->ciaddr;
    ack->yiaddr = assigned_ip;
    ack->siaddr = server->config.server_ip;
    ack->giaddr = request->giaddr;
    kmemcpy(ack->chaddr, request->chaddr, 16);
    ack->magic_cookie = DHCP_MAGIC_COOKIE;

    dhcp_set_message_type(ack, DHCP_ACK);

    uint8_t server_id_bytes[4];
    uint32_to_ip(server->config.server_ip, &server_id_bytes[0], &server_id_bytes[1],
                 &server_id_bytes[2], &server_id_bytes[3]);
    dhcp_add_option(ack, DHCP_OPT_SERVER_ID, 4, server_id_bytes);

    uint8_t subnet_bytes[4];
    uint32_to_ip(server->config.subnet_mask, &subnet_bytes[0], &subnet_bytes[1],
                 &subnet_bytes[2], &subnet_bytes[3]);
    dhcp_add_option(ack, DHCP_OPT_SUBNET_MASK, 4, subnet_bytes);

    uint8_t router_bytes[4];
    uint32_to_ip(server->config.gateway_ip, &router_bytes[0], &router_bytes[1],
                 &router_bytes[2], &router_bytes[3]);
    dhcp_add_option(ack, DHCP_OPT_ROUTER, 4, router_bytes);

    uint8_t dns_bytes[4];
    uint32_to_ip(server->config.dns_ip, &dns_bytes[0], &dns_bytes[1],
                 &dns_bytes[2], &dns_bytes[3]);
    dhcp_add_option(ack, DHCP_OPT_DNS_SERVER, 4, dns_bytes);

    uint8_t lease_bytes[4];
    uint32_t lease_time = server->config.lease_time;
    lease_bytes[0] = (lease_time >> 24) & 0xFF;
    lease_bytes[1] = (lease_time >> 16) & 0xFF;
    lease_bytes[2] = (lease_time >> 8) & 0xFF;
    lease_bytes[3] = lease_time & 0xFF;
    dhcp_add_option(ack, DHCP_OPT_LEASE_TIME, 4, lease_bytes);
}

/* Build a DHCP NAK message */
void dhcp_build_nak(dhcp_message_t *request, dhcp_message_t *nak) {
    kmemset(nak, 0, sizeof(dhcp_message_t));

    nak->op = 2;                /* BOOTREPLY */
    nak->htype = request->htype;
    nak->hlen = request->hlen;
    nak->hops = 0;
    nak->xid = request->xid;
    nak->secs = 0;
    nak->flags = request->flags;
    nak->ciaddr = 0;
    nak->yiaddr = 0;
    nak->siaddr = 0;
    nak->giaddr = request->giaddr;
    kmemcpy(nak->chaddr, request->chaddr, 16);
    nak->magic_cookie = DHCP_MAGIC_COOKIE;

    uint8_t msg_type = DHCP_NAK;
    kmemset(nak->options, 0, 308);
    nak->options[0] = DHCP_OPT_MESSAGE_TYPE;
    nak->options[1] = 1;
    nak->options[2] = msg_type;
    nak->options[3] = DHCP_OPT_END;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Table mode — server-level entry points
 * ───────────────────────────────────────────────────────────────────────── */
#if defined(DHCP_LEASE_MODE_TABLE)
void dhcp_init_server_table(dhcp_server_t *server, dhcp_config_t *config,
                            dhcp_lease_t *leases, uint16_t max_leases) {
    server->config = *config;
    dhcp_tablepool_init(&server->pool, config->pool_start, leases, max_leases);
}

void dhcp_process_message_table(dhcp_server_t *server, dhcp_message_t *request,
                                dhcp_message_t *response, uint32_t cur_time) {
    uint8_t msg_type = dhcp_get_message_type(request);
    dhcp_tablepool_t *pool = &server->pool;
    if (pool->lease_count >= pool->max_leases)
        dhcp_tablepool_cleanup_expire_lease(pool, cur_time); 

    switch (msg_type) {
        case DHCP_DISCOVER: {
            uint32_t offered_ip = dhcp_tablepool_find_available_ip(
                pool, server->config.pool_start, server->config.pool_end,
                request->chaddr, cur_time);
            if (offered_ip)
                dhcp_build_offer(server, request, response, offered_ip);
            break;
        }

        case DHCP_REQUEST: {
            uint8_t  length = 0;
            uint8_t *req_opt = dhcp_get_option(request, DHCP_OPT_REQUESTED_IP, &length);

            if (req_opt && length == 4) {
                uint32_t requested_ip = ((uint32_t)req_opt[0] << 24) |
                                        ((uint32_t)req_opt[1] << 16) |
                                        ((uint32_t)req_opt[2] <<  8) |
                                         (uint32_t)req_opt[3];

                if (requested_ip >= server->config.pool_start &&
                    requested_ip <= server->config.pool_end) {
                    if (dhcp_tablepool_alloc_lease(pool, requested_ip, request->chaddr, server->config.lease_time, cur_time))
                        dhcp_build_ack(server, request, response, requested_ip);
                    else
                        dhcp_build_nak(request, response);
                } else {
                    dhcp_build_nak(request, response);
                }
            }
            break;
        }

        default:
            break;
    }
}
#endif /* DHCP_LEASE_MODE_TABLE */

/* ─────────────────────────────────────────────────────────────────────────
 * BITMAP_VARTIME mode — server-level entry points
 * ───────────────────────────────────────────────────────────────────────── */
#if defined(DHCP_LEASE_MODE_BMVAR)
void dhcp_init_server_bmvar(dhcp_server_t *server, dhcp_config_t *config) {
    server->config = *config;
    dhcp_bmpool_var_init(&server->pool, config->pool_start, config->lease_time);
}

void dhcp_process_message_bmvar(dhcp_server_t *server, dhcp_message_t *request,
                                dhcp_message_t *response, uint32_t cur_time) {
    dhcp_bmpool_var_t *pool = &server->pool;
    uint8_t msg_type = dhcp_get_message_type(request);

    switch (msg_type) {
        case DHCP_DISCOVER: {
            uint32_t real_lease_time;
            uint32_t offered_ip = dhcp_bmpool_var_peek(pool, cur_time, &real_lease_time);
            if (offered_ip != 0) {
                server->config.lease_time = real_lease_time;
                dhcp_build_offer(server, request, response, offered_ip);
            }
            break;
        }

        case DHCP_REQUEST: {
            uint8_t length = 0;
            uint8_t *req_opt = dhcp_get_option(request, DHCP_OPT_REQUESTED_IP, &length);

            if (req_opt && length == 4) {
                uint32_t requested_ip = ((uint32_t)req_opt[0] << 24) |
                                        ((uint32_t)req_opt[1] << 16) |
                                        ((uint32_t)req_opt[2] <<  8) |
                                         (uint32_t)req_opt[3];

                if (requested_ip >= server->config.pool_start &&
                    requested_ip <= server->config.pool_end) {
                    if (dhcp_bmpool_var_commit_ip(pool, requested_ip))
                        dhcp_build_ack(server, request, response, requested_ip);
                    else
                        dhcp_build_nak(request, response);
                } else {
                    dhcp_build_nak(request, response);
                }
            }
            break;
        }
        default:
            break;
    }
}
#endif /* DHCP_LEASE_MODE_BMVAR */

/* ─────────────────────────────────────────────────────────────────────────
 * BITMAP_UNITIME mode — server-level entry points
 * ───────────────────────────────────────────────────────────────────────── */
#if defined(DHCP_LEASE_MODE_BMUNI)
void dhcp_init_server_bmuni(dhcp_server_t *server, dhcp_config_t *config) {
    server->config = *config;
    dhcp_bmpool_uni_init(&server->pool, config->pool_start, config->lease_time);
}

void dhcp_process_message_bmuni(dhcp_server_t *server, dhcp_message_t *request,
                                dhcp_message_t *response, uint32_t cur_time) {
    dhcp_bmpool_uni_t *pool = &server->pool;
    uint8_t msg_type = dhcp_get_message_type(request);

    switch (msg_type) {
        case DHCP_DISCOVER: {
            uint32_t offered_ip = dhcp_bmpool_uni_peek(pool, cur_time);
            if (offered_ip != 0) {
                dhcp_build_offer(server, request, response, offered_ip);
            }
            break;
        }

        case DHCP_REQUEST: {
            uint8_t length = 0;
            uint8_t *req_opt = dhcp_get_option(request, DHCP_OPT_REQUESTED_IP, &length);

            if (req_opt && length == 4) {
                uint32_t requested_ip = ((uint32_t)req_opt[0] << 24) |
                                        ((uint32_t)req_opt[1] << 16) |
                                        ((uint32_t)req_opt[2] <<  8) |
                                         (uint32_t)req_opt[3];

                if (requested_ip >= server->config.pool_start &&
                    requested_ip <= server->config.pool_end) {
                    if (dhcp_bmpool_uni_commit_ip(pool, requested_ip))
                        dhcp_build_ack(server, request, response, requested_ip);
                    else
                        dhcp_build_nak(request, response);
                } else {
                    dhcp_build_nak(request, response);
                }
            }
            break;
        }
        default:
            break;
    }
}
#endif /* DHCP_LEASE_MODE_BMUNI */

#if defined(DHCP_LEASE_MODE_NPRC)
void dhcp_init_server_nprc(dhcp_server_t *server, 
        dhcp_config_t *config) {
    server->config = *config;
    server->pool.offer_next = 0;
    server->pool.cache_base = 0;
    server->pool.ack_bitmap = 0;
}

void dhcp_process_message_nprc(dhcp_server_t *server, 
        dhcp_message_t *request, dhcp_message_t *response) {
    uint8_t msg_type = dhcp_get_message_type(request);

    switch (msg_type) {
        case DHCP_DISCOVER: {
            unsigned int offered_ip = dhcp_nprc_find_available_ip(&server->pool,
                    server->config.pool_start, server->config.pool_end);
            if (offered_ip)
                dhcp_build_offer(server, request, response, offered_ip);

            break;
        }

        case DHCP_REQUEST: {
            uint8_t  length = 0;
            uint8_t *req_opt = dhcp_get_option(request, DHCP_OPT_REQUESTED_IP, &length);

            if (req_opt && length == 4) {
                uint32_t requested_ip = ((uint32_t)req_opt[0] << 24) |
                                        ((uint32_t)req_opt[1] << 16) |
                                        ((uint32_t)req_opt[2] <<  8) |
                                         (uint32_t)req_opt[3];
             if (dhcp_nprc_commit_ip(&server->pool, requested_ip, 
                         server->config.pool_start, server->config.pool_end)) 
                dhcp_build_ack(server, request, response, requested_ip);
             else 
                dhcp_build_nak(request, response);
            }
             break;
        }
        default:
            break;
    }
}
#endif


/* ─────────────────────────────────────────────────────────────────────────
 * Hashmap mode — server-level entry points
 * ───────────────────────────────────────────────────────────────────────── */
#if defined(DHCP_LEASE_MODE_HASHMAP)
void dhcp_init_server_hashmap(dhcp_server_t *server, dhcp_config_t *config) {
    server->config = *config;
    dhcp_hashpool_init(&server->pool, config->pool_start);
}

void dhcp_process_message_hashmap(dhcp_server_t *server, dhcp_message_t *request,
                                dhcp_message_t *response, uint32_t cur_time) {
    uint8_t msg_type = dhcp_get_message_type(request);
    dhcp_hashpool_t *pool = &server->pool;
    if (hash_size(&pool->leases) >= HASH_N)
        dhcp_hashpool_cleanup_expire_lease(pool, cur_time);

    switch (msg_type) {
        case DHCP_DISCOVER: {
            uint32_t offered_ip = dhcp_hashpool_find_available_ip(
                pool, server->config.pool_start, server->config.pool_end,
                request->chaddr, cur_time);
            if (offered_ip)
                dhcp_build_offer(server, request, response, offered_ip);
            break;
        }

        case DHCP_REQUEST: {
            uint8_t  length = 0;
            uint8_t *req_opt = dhcp_get_option(request, DHCP_OPT_REQUESTED_IP, &length);

            if (req_opt && length == 4) {
                uint32_t requested_ip = ((uint32_t)req_opt[0] << 24) |
                                        ((uint32_t)req_opt[1] << 16) |
                                        ((uint32_t)req_opt[2] <<  8) |
                                         (uint32_t)req_opt[3];

                if (requested_ip >= server->config.pool_start &&
                    requested_ip <= server->config.pool_end) {
                    if (dhcp_hashpool_alloc_lease(pool, requested_ip, request->chaddr, server->config.lease_time, cur_time))
                        dhcp_build_ack(server, request, response, requested_ip);
                    else
                        dhcp_build_nak(request, response);
                } else {
                    dhcp_build_nak(request, response);
                }
            }
            break;
        }

        default:
            break;
    }
}
#endif /* DHCP_LEASE_MODE_HASHMAP */
