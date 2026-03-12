#ifndef __FRAME_H__
#define __FRAME_H__

enum dhcp_msg_type {
    DHCP_UNKNOWN = 0,   // used for internal parsing
    DHCP_DISCOVER = 1,
    DHCP_OFFER,
    DHCP_REQUEST,
    DHCP_DECLINE,
    DHCP_ACK,
    DHCP_NAK,
    DHCP_RELEASE,
    DHCP_INFORM,
};

enum dhcp_opt_type {
    OPT_PAD = 0,
    OPT_SUBNET_MASK = 1,
    OPT_ROUTER = 3,    // gateway
    OPT_REQUEST_IP = 50,
    OPT_LEASE_TIME = 51,
    OPT_MSG_TYPE = 53,
    OPT_SERVER_IP = 54,
    OPT_CLIENT_ID = 61,
    OPT_END = 255
};

struct build_meta {
    unsigned char client_mac[6];
    unsigned char requested_ip[4];
    unsigned char server_ip[4];
    enum dhcp_msg_type dhcp_type;
    int xid;
};

struct parse_meta {
    unsigned char server_mac[6];
    unsigned char offered_ip[4];
    unsigned char server_ip[4];
    unsigned char subnet_mask[4];
    enum dhcp_msg_type dhcp_type;
    int lease_time;
    int xid;
};


int build_frame(unsigned char *buf, int buflen, const struct build_meta meta);
void parse_frame(unsigned char *buf, int buflen, struct parse_meta *meta);
int parse_xid(unsigned char *buf, int buflen, unsigned int *xid);
void debug_frame(unsigned char *buf, int buflen); 

#endif
