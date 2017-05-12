#ifndef SERVER_H
#define SERVER_H

#include "channel.h"
#include "object.h"

#define MAX_OBJECTS_LIST 100
#define BACKLOG 10

typedef struct {
	int client_id;
	channel_info channel;
}client_info;

typedef struct {
	int server_id;
	channel_info channel;
}server_info;

typedef struct {
	int id;
	int num_clients;
	int num_servers;
	object_st objects[MAX_OBJECTS_LIST];
	channel_info server_state;
	server_info servers_state[MAX_SERVERS];
	client_info clients_state[MAX_CLIENTS];
	int inputfd;
	STATE state;
	int timestamp;
}server_process_state;
#endif
