#ifndef __SERVER_H
#define __SERVER_H

#define MAX_SERVERS 8
#define SERVER_PORT 29871
#define BACKLOG 10
char *servers_names[MAX_SERVERS] =  {
	"dc01.utdallas.edu",
	"dc02.utdallas.edu",
	"dc03.utdallas.edu",
	"dc04.utdallas.edu",
	"dc05.utdallas.edu",
	"dc06.utdallas.edu",
	"dc07.utdallas.edu",
	"dc08.utdallas.edu"
};

typedef enum {
	CHANNEL_STATE_INITED,
	CHANNEL_STATE_CONNECTING,
	CHANNEL_STATE_CONNECTED,
	CHANNEL_STATE_DISCONNECTED,
	CHANNEL_STATE_MAX,
} CHANNEL_STATE;

typedef struct {
	int fd;
	struct sockaddr_in addr;
	CHANNEL_STATE state;
} channel_info;

typedef struct {
	int id;
	int value;
	int version;
	int ru;
	int ds;
} object_info;

typedef struct {
	int id;
	channel_info ch_info;
	object_info object;
} server_info;

typedef struct {
	int id;
	server_info servers[MAX_SERVERS];
	int num_votes_req;
	int num_votes_resp;
	int num_write_requests;
	int num_writes;
	object_info write_object;
	int write_pending;
} server_st;

typedef enum {
	VOTE_REQ,
	VOTE_RESP,
	WRITE_REQ,
	WRITE_RESP,
	UPDATE_REQ,
	UPDATE_RESP,
	READ_REQ,
	READ_RESP,
} MESSAGE_TYPE;

typedef struct {
	MESSAGE_TYPE m;
	object_info object;
} server_message;

#endif
