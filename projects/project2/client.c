#include "client.h"

int get_server_info(const char *server_name, channel_info *channel) {
	struct addrinfo hints, *res0;
	int error;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET;
	error = getaddrinfo(server_name, NULL, &hints, &res0);
	if (error) {
		perror("getaddrinfo fail:");
		return -1;
	} else {
		memcpy(&(channel->addr), res0->ai_addr, sizeof(struct sockaddr_in));
		channel->addr.sin_port = htons(SERVER_PORT);
		printf("%s: %s\n", server_name, inet_ntoa(channel->addr.sin_addr));
		freeaddrinfo(res0);
		return 0;
	}
}

int get_addr_info() {
	struct sockaddr_in *address = NULL;
	struct ifaddrs *addrs;
	getifaddrs(&addrs);
	struct ifaddrs * tmp = addrs;
	while (tmp != NULL) {
		if (tmp->ifa_addr && tmp->ifa_addr->sa_family == AF_INET &&
			(tmp->ifa_flags & IFF_LOOPBACK) != IFF_LOOPBACK) {
		        address = (struct sockaddr_in *)tmp->ifa_addr;
			printf("%s: %s\n", tmp->ifa_name, inet_ntoa(address->sin_addr));
			break;
		}
		tmp = tmp->ifa_next;
	}
	if (address == NULL) {
		perror("Error address not found: ");
		return -1;
	} else {
		memcpy(&s_process_state.addr, address, sizeof(struct sockaddr_in));
		s_process_state.addr.sin_port = htons(CLIENT_PORT);
		freeifaddrs(addrs);
		return 0;
	}
}

int initialize_and_connect_servers() {
	int i = 0;
	for (i = 0 ; i < MAX_SERVERS; i++) {
		server_state server = s_process_state.servers[i];
		server.id = i;
		channel_info channel = server.channel;
		channel.state = COMM_STATE_INITIALIZED;
		channel.sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (channel.sockfd < 0) {
			perror("Socket init failed: ");
			return -1;
		}
		struct sockaddr_in caddr;
		memcpy(&caddr, &s_process_state.addr, sizeof(struct sockaddr_in));
		caddr.sin_port = htons(CLIENT_PORT + i + 1 + s_process_state.id*MAX_SERVERS);
		int reuseAddr = 1;
		if (setsockopt(channel.sockfd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr)) < 0) {
			perror("Failed to set reuse address option:");
		}
		if (bind(channel.sockfd, (struct sockaddr *)&(caddr), sizeof(struct sockaddr_in)) < 0) {
			perror("Socket Bind failed: ");
			close(channel.sockfd);
			return -1;
		}
		channel.state = COMM_STATE_CONNECTING;
		get_server_info(servers_list[i], &channel);
		printf("Connecting to %s:%u via %d %u\n", inet_ntoa(channel.addr.sin_addr), ntohs(channel.addr.sin_port), channel.sockfd, ntohs(caddr.sin_port));
		if (connect(channel.sockfd, (struct sockaddr *)&(channel.addr), sizeof(struct sockaddr_in)) < 0) {
			perror("Socket connection failed: ");
			channel.state = COMM_STATE_INITIALIZED;
			close(channel.sockfd);
			return -1;
		}
		channel.state = COMM_STATE_CONNECTED;
		server.channel = channel;
		s_process_state.servers[i] = server;
	}
	return 0;
}

int initialize(int process_id) {
	s_process_state.id = process_id;
	// get addr of self and populate in s_process_state.addr
	if (get_addr_info() < 0) {
		return -1;
	}
	if (initialize_and_connect_servers() < 0) {
		return -1;
	}
	s_process_state.inputfd = STDIN_FILENO;
	s_process_state.operation.locked = 0;
	s_process_state.operation.lock_req_sent = 0;
	s_process_state.operation.lock_resp_recvd = 0;
	memset(s_process_state.operation.query_recvd, 0, 3);
	s_process_state.operation.writes_done = 0;
	s_process_state.operation.write_pending = 0;
	s_process_state.timestamp = 0;
	s_process_state.seqno = 0;
	s_process_state.write_object_id = -1;
	s_process_state.write_object_value = -1;
	s_process_state.times_to_write = 0;
	return 0;
}

void setState(STATE state) {
	s_process_state.state = state;
	s_process_state.seqno++;
}

int sendCommand(ServerMessage message, int server_id) {
	int sockfd = s_process_state.servers[server_id].channel.sockfd;
	printf("TRACE(%d): state(%d) channelstate (%d)\n", s_process_state.timestamp, s_process_state.state, s_process_state.servers[server_id].channel.state);
	if (s_process_state.servers[server_id].channel.state == COMM_STATE_CONNECTED ||
		s_process_state.state == STATE_CONTROL_PROCESSING) {
		s_process_state.timestamp += DIFF;
		message.timestamp = s_process_state.timestamp;
		message.seqno = s_process_state.seqno;
		int send_size = send(sockfd, &message, sizeof(message), 0);
		if (send_size != sizeof(message)) {
			perror("Failed to send message:");
		}
		return send_size;
	} else {
		printf("Channel not connected. Adding to pending messages\n");
		return -1;
	}
}

int sendControlMessage(char *command, char *server_id) {
	ServerMessage message;
	message.command_id = strncmp(command, "SUSPEND", 7)?MESSAGE_RESUME:MESSAGE_SUSPEND;
	message.object_id = 0;
	message.object_value = 0;

	if (s_process_state.state != STATE_INITIALIZED) {
		printf("BUSY. In state %d\n", s_process_state.state);
		return -1;
	}

	int id = atoi(server_id);
	setState(STATE_CONTROL_PROCESSING);
	if (sendCommand(message, atoi(server_id)) < 0) {
		printf("Send failure\n");
		setState(STATE_INITIALIZED);
		return -1;
	}
	s_process_state.servers[id].channel.state = strncmp(command, "SUSPEND", 7)?COMM_STATE_RESUMING:COMM_STATE_SUSPENDING;
	return 0;
}

int sendUnlockCommand(int object_id) {
	setState(STATE_UNLOCKING);
	printf("TRACE(%d): UNLOCKing\n", s_process_state.timestamp);
	ServerMessage message = s_process_state.operation.pending_operation;
	int server_id = hash(message.object_id);
	ServerMessage unlockMessage;
	unlockMessage.command_id = MESSAGE_UNLOCK;

	if (object_id == -1)
		unlockMessage.object_id = message.object_id;
	else {
		unlockMessage.object_id = object_id;
		server_id = hash(object_id);
	}
	unlockMessage.object_value = 0;

	if (server_id >= 0) {
		sendCommand(unlockMessage, server_id);
		sendCommand(unlockMessage, (server_id+1)%7);
		sendCommand(unlockMessage, (server_id+2)%7);
		s_process_state.operation.query_recvd[0] = 0;
		s_process_state.operation.query_recvd[1] = 0;
		s_process_state.operation.query_recvd[2] = 0;
	}
	return 0;
}

int sendLockCommand(int object_id) {
	int server1 = hash(object_id);
	int server2 = (server1 + 1)%7;
	int server3 = (server1 + 2)%7;

	printf("TRACE(%d): obj(%d) server1(%d) server2(%d) server3(%d)\n", s_process_state.timestamp, object_id, server1, server2, server3);
	setState(STATE_LOCKING);
	ServerMessage message;
	message.command_id = MESSAGE_LOCK;
	message.object_id = object_id;
	message.object_value = 0;

	int result1 = sendCommand(message, server1);
	if (result1 > 0) {
		s_process_state.operation.lock_req_sent++;
	}
	int result2 = sendCommand(message, server2);
	if (result2 > 0) {
		s_process_state.operation.lock_req_sent++;
	}
	int result3 = sendCommand(message, server3);
	if (result3 > 0) {
		s_process_state.operation.lock_req_sent++;
	}
	if ((result1>0 && result2>0) ||
		(result2>0 && result3>0) ||
		(result3>0 && result1>0)) {
		return 0;
	} else {
		printf("Lock command send failed\n");
		sendUnlockCommand(object_id);
		return -1;
	}
}

int insertUpdateOperationI(char *command, int object_id, int object_value) {
	printf("TRACE(%d): %s %d %d %d\n", s_process_state.timestamp, command, object_id, object_value, s_process_state.times_to_write);
	if (s_process_state.state != STATE_INITIALIZED) {
		printf("BUSY. In state %d\n", s_process_state.state);
		return -1;
	}
	int command_id = (strncmp(command, "WRITE", 5))?MESSAGE_UPDATE:MESSAGE_WRITE;
	ServerMessage message;
	message.command_id = command_id;
	message.object_id = object_id;
	message.object_value = object_value;
	s_process_state.operation.locked = 0;
	s_process_state.operation.lock_resp_recvd = 0;
	s_process_state.operation.lock_req_sent = 0;
	s_process_state.operation.query_recvd[0] = 0;
	s_process_state.operation.query_recvd[1] = 0;
	s_process_state.operation.query_recvd[2] = 0;
	s_process_state.operation.writes_done = 0;
	s_process_state.operation.pending_operation = message;

	s_process_state.write_object_id = object_id;
	s_process_state.write_object_value = object_value;

	if (sendLockCommand(object_id) < 0) {
		setState(STATE_INITIALIZED);
		printf("Lock Send failed\n");
		return -1;
	}
	s_process_state.operation.write_pending = 1;
	return 0;
}

int insertUpdateOperation(char *command, char *object, char *value) {
	int object_id = atoi(object);
	int object_value = atoi(value);

	return insertUpdateOperationI(command, object_id, object_value);
}

int readOperation(char *command, char *object) {
	ServerMessage message;
	message.command_id = MESSAGE_READ;
	message.object_id = atoi(object);
	
	if (s_process_state.state != STATE_INITIALIZED) {
		printf("BUSY. In state %d\n", s_process_state.state);
		return -1;
	}

	int server_id = hash(atoi(object));
	int congestion_control = rand()%3;
	server_id = (server_id + congestion_control)%MAX_SERVERS;
	printf("Object id:%d server id: %d\n", message.object_id, server_id);
	if (server_id >= 0) {
		setState(STATE_READING);
		if (sendCommand(message, server_id) <= 0) {
			perror("Cannot send read command\n");
			setState(STATE_INITIALIZED);
		}
	} else {
		setState(STATE_INITIALIZED);
		printf("Hash failure. Cannot retrieve object value\n");
	}
	return 0;
}

void performLockUnlock(char *token, char *object) {
	MESSAGE_TYPES command_id = (strncmp(token, "LOCK", 4))?MESSAGE_UNLOCK:MESSAGE_LOCK;
	int object_id = atoi(object);
	switch(command_id) {
	case MESSAGE_LOCK:
		sendLockCommand(object_id);
		break;
	case MESSAGE_UNLOCK:
		sendUnlockCommand(object_id);
		break;
	default:
		printf("Unknown command?\n");
	}
}

void parseInput(char *buffer) {
	if (strlen(buffer) < 3)
		return;
	char *token = strtok(buffer, " ");
	char *value1 = strtok(NULL, " ");
	if (token == NULL || value1 == NULL) {
		return;
	}

	if (!strncmp(token, "SUSPEND", 7) ||
		!strncmp(token, "RESUME", 6)) {
		sendControlMessage(token, value1);
	} else if (!strncmp(token, "WRITE", 5) ||
			!strncmp(token, "UPDATE", 6)) {
		char *value2 = strtok(NULL, " ");
		char *value3 = strtok(NULL, " ");
		if (value2 == NULL) {
			return;
		}

		if (value3 != NULL) {
			s_process_state.times_to_write = atoi(value3);
			s_process_state.times_to_write -= 1;
			if (s_process_state.times_to_write < 0) {
				return;
			}
		}
		printf("TRACE(%d): Value2 %s Value3 %s\n", s_process_state.timestamp, value2, value3);
		insertUpdateOperation(token, value1, value2);
	} else if (!strncmp(token, "READ", 4)) {
		readOperation(token, value1);
	} else if (!strncmp(token, "LOCK", 4) ||
			!strncmp(token, "UNLOCK", 5)) {
		performLockUnlock(token, value1);
	} else {
		printf("Bad input\n");
	}
}

int get_server_by_sockfd(int sockfd) {
	int index = 0;
	for (index = 0; index < MAX_SERVERS; index++) {
		if (s_process_state.servers[index].channel.sockfd == sockfd)
			return index;
	}
	return -1;
}

int startWriteOperation() {
	setState(STATE_WRITING);
	printf("TRACE(%d): Locked. WRITING\n", s_process_state.timestamp);
	ServerMessage message = s_process_state.operation.pending_operation;

	int server_id = hash(message.object_id);
	if (server_id >= 0) {
		if (s_process_state.operation.resp_recvd[server_id] != -1)
			sendCommand(message, server_id);
		if (s_process_state.operation.resp_recvd[(server_id+1)%7] != -1)
			sendCommand(message, (server_id+1)%7);
		if (s_process_state.operation.resp_recvd[(server_id+2)%7] != -1)
			sendCommand(message, (server_id+2)%7);
	}
	return 0;
}

int sendQueryResponse() {
	int i = 0;
	int server_id = hash(s_process_state.operation.pending_operation.object_id);
	for (i = 0; i < 3; i++) {
		if (s_process_state.operation.query_recvd[i] == 1) {
			ServerMessage message;
			message.command_id = MESSAGE_QUERY_RESP;
			message.object_id = s_process_state.operation.pending_operation.object_id;
			message.object_value = 0;
			sendCommand(message, (server_id + i)%7);
			s_process_state.operation.locked -= 1;
			s_process_state.operation.lock_resp_recvd -=1;
			printf("TRACE(%d): lock_resp_recvd(%d) locc_req_sent(%d)\n", s_process_state.timestamp, s_process_state.operation.lock_resp_recvd, s_process_state.operation.lock_req_sent);
			if (s_process_state.operation.locked < 0)
				s_process_state.operation.locked = 0;
			if (s_process_state.operation.lock_resp_recvd < 0)
				s_process_state.operation.lock_resp_recvd = 0;
			s_process_state.operation.resp_recvd[i] = -1;
			s_process_state.operation.query_recvd[i] = 0;
		}
	}
	return 0;
}

int processServerMessage(ServerMessage message, int sockfd) {
	s_process_state.timestamp = (s_process_state.timestamp > message.timestamp)?s_process_state.timestamp:message.timestamp;
	s_process_state.timestamp += DIFF;
	ServerMessage response;
	printf("TRACE(%d): recv command(%d) fd(%d)\n", s_process_state.timestamp, message.command_id, sockfd);
	int server_id = get_server_by_sockfd(sockfd);
	int resp;
	if (server_id < 0)
		return -1;
	switch(message.command_id) {
	case MESSAGE_SUSPEND:
		setState(STATE_CONTROL_PROCESSING);
		response.command_id = MESSAGE_SUSPEND_RESP;
		response.object_id = 0;
		response.object_value = 0;
		sendCommand(response, server_id);
		setState(STATE_INITIALIZED);
		s_process_state.servers[server_id].channel.state = COMM_STATE_SUSPENDED;	
		break;
	case MESSAGE_RESUME:
		setState(STATE_CONTROL_PROCESSING);
		response.command_id = MESSAGE_RESUME_RESP;
		response.object_id = 0;
		response.object_value = 0;
		sendCommand(response, server_id);
		setState(STATE_INITIALIZED);
		s_process_state.servers[server_id].channel.state = COMM_STATE_CONNECTED;
		break;
	case MESSAGE_SUSPEND_RESP:
		setState(STATE_INITIALIZED);
		s_process_state.servers[server_id].channel.state = COMM_STATE_SUSPENDED;
		break;
	case MESSAGE_RESUME_RESP:
		setState(STATE_INITIALIZED);
		s_process_state.servers[server_id].channel.state = COMM_STATE_CONNECTED;
		break;
	case MESSAGE_READ_RESP:
		if (message.object_id >= 0) {
			printf("OBJECT ID: %d VALUE: %d\n", message.object_id, message.object_value);
		} else {
			if (message.object_value == -1) {
				printf("NO SUCH OBJECT EXISTS ERROR FROM SERVER\n");
			} else if (message.object_value == -2) {
				printf("INVALID OBJECT ID RETURNED FROM SERVER\n");
			}
		}
		setState(STATE_INITIALIZED);
		break;
	case MESSAGE_LOCK_RESP:
		resp = message.object_id;
		printf("TRACE(%d): Lock Response(%d)\n", s_process_state.timestamp, resp);
		printf("TRACE(%d): lock_resp_recvd(%d) locc_req_sent(%d)\n", s_process_state.timestamp, s_process_state.operation.lock_resp_recvd, s_process_state.operation.lock_req_sent);
		if (resp > 0 && s_process_state.operation.locked < s_process_state.operation.lock_req_sent) {
			s_process_state.operation.locked++;
		}
		s_process_state.operation.resp_recvd[server_id - hash(s_process_state.operation.pending_operation.object_id)] = resp;
		s_process_state.operation.lock_resp_recvd++;
		if (s_process_state.operation.lock_resp_recvd > s_process_state.operation.lock_req_sent) {
			s_process_state.operation.lock_resp_recvd = s_process_state.operation.lock_req_sent;
		}
		if (s_process_state.operation.lock_resp_recvd == s_process_state.operation.lock_req_sent &&
				s_process_state.state != STATE_WRITING) {
			if (s_process_state.operation.locked >= 2) {
				setState(STATE_LOCKED);
				if (s_process_state.operation.write_pending == 1) {
					startWriteOperation();
				}
			} else {
				sendQueryResponse();
			}
		} else if (s_process_state.state == STATE_WRITING && s_process_state.operation.write_pending == 1) {
			ServerMessage writeMessage;
			writeMessage = s_process_state.operation.pending_operation;
			if (resp > 0) {
				sendCommand(writeMessage, server_id);
			}
		}
		break;
	case MESSAGE_WRITE_RESP:
	case MESSAGE_UPDATE_RESP:
		s_process_state.operation.writes_done++;
		if (s_process_state.operation.pending_operation.object_id != message.object_id) {
			printf("TRACE(%d): Unknown write operation\n", s_process_state.timestamp);
		}

		if (s_process_state.operation.pending_operation.object_value != message.object_value) {
			printf("TRACE(%d) Write Failed\n", s_process_state.timestamp);
		}
		if (s_process_state.operation.writes_done == s_process_state.operation.locked) {
			setState(STATE_WRITE_DONE);
			s_process_state.operation.write_pending = 0;
			sendUnlockCommand(-1);
		}
		break;
	case MESSAGE_UNLOCK_RESP:
		s_process_state.operation.locked -= 1;
		if (s_process_state.operation.locked < 0)
			s_process_state.operation.locked = 0;
		s_process_state.operation.lock_resp_recvd -=1 ;
		if (s_process_state.operation.lock_resp_recvd < 0)
			s_process_state.operation.lock_resp_recvd = 0;
		s_process_state.operation.lock_req_sent -= 1;
		s_process_state.operation.resp_recvd[server_id - hash(s_process_state.operation.pending_operation.object_id)] = -1;
		printf("TRACE(%d): lock_resp_recvd(%d) locc_req_sent(%d)\n", s_process_state.timestamp, s_process_state.operation.lock_resp_recvd, s_process_state.operation.lock_req_sent);
		s_process_state.operation.writes_done = 0;
		s_process_state.operation.query_recvd[0] = 0;
		s_process_state.operation.query_recvd[1] = 0;
		s_process_state.operation.query_recvd[2] = 0;
		if (s_process_state.operation.locked == 0)
			setState(STATE_INITIALIZED);
		if (s_process_state.times_to_write > 0 && s_process_state.operation.locked == 0) {
			s_process_state.times_to_write -= 1;
			usleep(rand()%20000);
			insertUpdateOperationI("WRITE", s_process_state.write_object_id , s_process_state.write_object_value);
		}
		break;
	case MESSAGE_QUERY:
		printf("TRACE(%d): QUERY Received\n", s_process_state.timestamp);
		s_process_state.operation.query_recvd[server_id - hash(s_process_state.operation.pending_operation.object_id)] = 1;
		if (s_process_state.operation.lock_resp_recvd == s_process_state.operation.lock_req_sent && s_process_state.state != STATE_LOCKED) {
			printf("TRACE(%d): lock responses(%d) lock requests(%d) locked(%d)\n", s_process_state.timestamp,
				s_process_state.operation.lock_resp_recvd, s_process_state.operation.lock_req_sent,
				s_process_state.operation.locked);
			if (s_process_state.operation.locked < 2) {
				sendQueryResponse();
			}
		}
		break;
	default:
		printf("Unknown message from server\n");
	}
	return 0;
}

int processFd(int fd) {
	char buf[MAX_BUFSIZE] = {'\0', };
	ServerMessage message;
	if (fd == 0) {
		s_process_state.timestamp += DIFF;
		int read_size = read(fd, buf, MAX_BUFSIZE);
		if (read_size < 0) {
			return -1;
		}
		if (buf[strlen(buf)-1] == '\n') {
			buf[strlen(buf)-1] = '\0';
		}
		printf("Buffer = %s\n", buf);
		parseInput(buf);
	} else {
		int recv_size = recv(fd, &message, sizeof(message), 0);
		if (recv_size <= 0) {
			return -1;
		}
		processServerMessage(message, fd);
	}
	return 0;
}

void start() {
	int index = 0;
	fd_set fds, localfds;
	FD_ZERO(&fds);
	FD_ZERO(&localfds);
	int maxfd = 0;

	printf("Adding input fileno to fds\n");
	FD_SET(STDIN_FILENO, &fds);
	for (index = 0; index < MAX_SERVERS; index++) {
		if (s_process_state.servers[index].channel.state == COMM_STATE_CONNECTED) {
			printf("Adding %d to fds\n", s_process_state.servers[index].channel.sockfd);
			FD_SET(s_process_state.servers[index].channel.sockfd, &fds);
			if (maxfd < s_process_state.servers[index].channel.sockfd) {
				maxfd = s_process_state.servers[index].channel.sockfd;
			}
		}
	}

	int done = 0;
	while (!done) {
		FD_ZERO(&localfds);
		localfds = fds;
		fflush(NULL);
		int select_result = select(maxfd+1, &localfds, NULL, NULL, NULL);
		if (select_result < 0) {
			perror("Select failed: ");
			done = 1;
			break;
		} else {
			int i = 0;
			for (i = 0; i < maxfd+1; i++) {
				if (FD_ISSET(i, &localfds)) {
					if (processFd(i) < 0) {
						close(i);
						FD_CLR(i, &fds);
						if (maxfd == i) {
							maxfd--;
						}
					}
				}
			}
		}
	}
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		printf("Usage: client <id>\n");
		return -1;
	}
	int id = atoi(argv[1]);
	// initialize global state
	initialize(id);
	start();
	return 0;
}
