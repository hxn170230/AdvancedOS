/*
 * AUTHOR: HARSHAVARDHAN NALAJALA
 * UTD ID: 2021346835
 * EMAIL ID: hxn170230@utdallas.edu
 */
#ifndef _CONF_
#define _CONF_

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define N_PROCESSES 10 // number of processes in total
#define SERVER_PORT 25000 // server port
#define BACKLOG 10 // number of simultaneous connection requests for listen
#define CLIENT_PORT 27000 // client initial port
#define MAX_TRIES 20 // maximum tries before client closes

// RANDOM variables to PHASE 1 and PHASE 2 computation
#define PHASE_1_RAND 6
#define PHASE_1_DELAY 5
#define PHASE_2_DELAY 45
#define MAX_CRITICAL_SECTION_ENTRIES 40
// Termination detector node
#define ROOT_NODE 0

// Server IP: Process 0 is assumed to be server
const char *serverIP0 = "10.176.66.51";
// IPs of processes in Distributed System
// Hardcoded since method to attach the processes in the distributed system is out of scope
const char *serverIPs[N_PROCESSES] = {
	"10.176.66.51", // DC01
	"10.176.66.52", // DC02
	"10.176.66.53", // DC03
	"10.176.66.54", // DC04
	"10.176.66.55", // DC05
	"10.176.66.56", // DC06
	"10.176.66.57", // DC07
	"10.176.66.58", // DC08
	"10.176.66.59", // DC09
	"10.176.66.60", // DC10
};

/* communication link states */
typedef enum {
	UNINITED,
	INITING,
	INITED,
	CONNECTING,
	CONNECTED,
	DISCONNECTING,
	DISCONNECTED,
}COMM_STATE;

/* Process states */
typedef enum {
	PROCESS_UNINITED,
	PROCESS_INITING,
	PROCESS_INITED,
}PROCESS_STATE;

/* communication link structure */
typedef struct comm {
	int processId;					// process id
	int incomingSocket;				// incoming socket
	int outgoingSocket;				// outgoing socket
	COMM_STATE incomingState;			// incoming socket state
	COMM_STATE outgoingState;			// outgoing socket state
	struct sockaddr_in addr;			// process address in the network
}communication_link;

/* Server status */
typedef struct s_server{
	int serverSocket;
	COMM_STATE serverSocketState;
	struct sockaddr_in addr;
}server;

/* Upper Layer: Mutual Exclusion (Ricart Agarwala Algorithm states) */
typedef struct s_upperLayer {
	int requestTimeStamp;
	int haveToken;
	int repliesReceived;
	int messagesExchanged;
	int messagesSent;
	int messagesReceived;
	int totalMessagesSent;
	int totalMessagesReceived;
	struct timeval timeOfRequest;
	int timerStart;
	int timerValue;
}nextLayer;

/* Global Process Data Structure */
typedef struct process {
	int number; 					// process identifier
	int diff; 					// 'd' to update scalar/vector clocks
	PROCESS_STATE state;
	int knownEvents[N_PROCESSES][N_PROCESSES]; 	// causal data to identify messages order
	communication_link channels[N_PROCESSES];  	// communication channel 
	server server_link;
	int time;

	nextLayer upperState;
	int deferredList[N_PROCESSES];
	int deferredCount;

	int phase;
	int computation_end;
}process_state;

/* Types of messages exchanged in the Distributed System */
typedef enum {
	REQUEST_CRITICAL_SECTION,
	REPLY_CRITICAL_SECTION,
	COMPUTATION_END,
}MESSAGE_TYPE;

/* Structure of the message exchanged in Distributed System */
typedef struct s_message {
	MESSAGE_TYPE type;
	int timestamp;
}message;

#endif
