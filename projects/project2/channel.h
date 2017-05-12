#ifndef CHANNEL_H
#define CHANNEL_H

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SERVER_PORT 27000
#define CLIENT_PORT 29000
#define MAX_BUFSIZE 100
#define MAX_SERVERS 7
#define MAX_CLIENTS 5
#define DIFF 5

char * servers_list[] = {
	"DC01",
	"DC02",
	"DC03",
	"DC04",
	"DC05",
	"DC06",
	"DC07"
};

typedef enum {
	COMM_STATE_UNINITIALIZED,
	COMM_STATE_INITIALIZED,
	COMM_STATE_CONNECTING,
	COMM_STATE_CONNECTED,
	COMM_STATE_DISCONNECTING,
	COMM_STATE_DISCONNECTED,
	COMM_STATE_SUSPENDING,
	COMM_STATE_SUSPENDED,
	COMM_STATE_RESUMING,
	COMM_STATE_RESUMED,
	COMM_STATE_MAX_COMM_STATE,
}COMM_STATE;

typedef struct {
	COMM_STATE state;
	int sockfd;
	struct sockaddr_in addr;
}channel_info;

typedef enum {
	MESSAGE_LOCK,
	MESSAGE_UNLOCK,
	MESSAGE_LOCK_RESP,
	MESSAGE_UNLOCK_RESP,
	MESSAGE_QUERY,
	MESSAGE_QUERY_RESP,
	MESSAGE_WRITE,
	MESSAGE_WRITE_RESP,
	MESSAGE_UPDATE,
	MESSAGE_UPDATE_RESP,
	MESSAGE_READ,
	MESSAGE_READ_RESP,
	MESSAGE_SUSPEND,
	MESSAGE_SUSPEND_RESP,
	MESSAGE_RESUME,
	MESSAGE_RESUME_RESP,
	MESSAGE_AGREE,
	MESSAGE_TYPE_MAX,
}MESSAGE_TYPES;

typedef struct {
	int command_id;
	int object_id;
	int object_value;
	int timestamp;
	int seqno;
	int client_id;
}ServerMessage;

typedef enum {
	STATE_INITIALIZED,
	STATE_LOCKING,
	STATE_LOCKED,
	STATE_UNLOCKING,
	STATE_READING,
	STATE_READ_DONE,
	STATE_WRITING,
	STATE_WRITE_DONE,
	STATE_CONTROL_PROCESSING,
	STATE_CONTROL_PROCESSING_DONE,
	STATE_MAX,
}STATE;

int hash(int key) {
	// Knuth's multiplicative hash method
	// int h = (object_id*2654435761)%2^31;
	key = ~key + (key << 15); // key = (key << 15) - key - 1;
	key = key ^ (key >> 12);
	key = key + (key << 2);
	key = key ^ (key >> 4);
	key = key * 2057; // key = (key + (key << 3)) + (key << 11);
	key = key ^ (key >> 16);

	return (key)%7;
}
#endif
