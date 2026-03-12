#ifndef __CLIENT_QUEUE_H__
#define __CLIENT_QUEUE_H__


#include "dhcp-client.h"

void client_queue_init();
struct client_async * pop_client();
void push_client(struct client_async *c_async);

#endif
