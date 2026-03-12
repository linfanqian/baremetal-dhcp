#ifndef __XID_TAB_H__
#define __XID_TAB_H__

#include "dhcp-client.h"

void xid_tab_init();
void xid_tab_insert(struct client_async *c_async);
struct client_async *xid_tab_lookup(uint32_t xid); 
void xid_tab_remove(uint32_t xid);

#endif
