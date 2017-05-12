#ifndef CLIENT_H
#define CLIENT_H

#include "channel.h"

typedef struct {
	int id;
	channel_info channel;
}server_state;

typedef struct {
	int locked;
	int lock_resp_recvd;
	int lock_req_sent;
	int query_recvd[3];
	int resp_recvd[3];
	int writes_done;

	int write_pending;
	ServerMessage pending_operation;
}operation_st;

typedef struct {
	int id;
	struct sockaddr_in addr;
	server_state servers[MAX_SERVERS];
	int inputfd;
	STATE state;
	operation_st operation;
	int timestamp;
	int seqno;

	int write_object_id;
	int write_object_value;
	int times_to_write;
}process_state;

static process_state s_process_state;
#endif
