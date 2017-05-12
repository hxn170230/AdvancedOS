#include "server.h"

static server_process_state s_process_state;
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
                memcpy(&(s_process_state.server_state.addr), address, sizeof(struct sockaddr_in));
                s_process_state.server_state.addr.sin_port = htons(SERVER_PORT);
		freeifaddrs(addrs);
                return 0;
        }
}

int initialize(int id) {
	memset(&s_process_state, 0, sizeof(s_process_state));
	s_process_state.id = id;
	s_process_state.num_clients = 0;
	s_process_state.num_servers = 0;
	int i = 0;
	int j = 0;
	for (j = 0; j < MAX_OBJECTS_LIST; j++) {
		for (i = 0; i < OUTSTANDING_REQUESTS_MAX; i++) {
			s_process_state.objects[j].request_list[i].timestamp = -1;
			s_process_state.objects[j].request_list[i].client_id = -1;
		}
		s_process_state.objects[j].request_list_size = 0;
	}
	channel_info server = s_process_state.server_state;
	server.state = COMM_STATE_INITIALIZED;
	server.sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	printf("Server socket = %d\n", server.sockfd);
	if (server.sockfd < 0) {
		perror("Socket init failure:");
		return -1;
	}
	get_addr_info();
	int reuseAddr = 1;
	if (setsockopt(server.sockfd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr)) < 0) {
		perror("Failed to set reuse address option:");
	}

	printf("Binding to %s:%u via %d\n", inet_ntoa(s_process_state.server_state.addr.sin_addr), ntohs(s_process_state.server_state.addr.sin_port), server.sockfd);
	if (bind(server.sockfd, (struct sockaddr *)&(s_process_state.server_state.addr), sizeof(struct sockaddr_in)) < 0) {
		perror("Server bind failed: ");
		return -1;
	}

	if (listen(server.sockfd, BACKLOG) < 0) {
		perror("Listen failure: ");
		return -1;
	}
	printf("Listening on %d\n", server.sockfd);
	s_process_state.server_state = server;
	s_process_state.timestamp = 0;
	return 0;
}

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

int connect_to_server(int id) {
	server_info server = s_process_state.servers_state[id];
	server.server_id = id;
	channel_info channel = server.channel;
	if (get_server_info(servers_list[id], &channel) < 0) {
		perror("Unable to find ip address:");
		return -1;
	}
	channel.state = COMM_STATE_INITIALIZED;
	channel.sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (channel.sockfd < 0) {
		perror("Socket init failed: ");
		return -1;
	}
	struct sockaddr_in caddr;
	memcpy(&caddr, &s_process_state.server_state.addr, sizeof(struct sockaddr_in));
	int i = 0;
	int bind_result = 0;
	while (i < 10) {
		caddr.sin_port = htons(SERVER_PORT + s_process_state.id + 1 + i);
		bind_result = bind(channel.sockfd, (struct sockaddr *)&caddr, sizeof(struct sockaddr_in));
		if (bind_result >= 0) break;
		i++;
	}

	if (bind_result < 0) {
		perror("Bind failed: ");
		close(channel.sockfd);
		return -1;
	}
	channel.state = COMM_STATE_CONNECTING;
	printf("Trying to connect to %s:%u via socket(%d) port(%u)\n", inet_ntoa(channel.addr.sin_addr), ntohs(channel.addr.sin_port), channel.sockfd, ntohs(caddr.sin_port));
	if (connect(channel.sockfd, (struct sockaddr *) &channel.addr, sizeof(struct sockaddr_in)) < 0) {
		perror("Failed to connect: ");
		channel.state = COMM_STATE_INITIALIZED;
		close(channel.sockfd);
		return -1;
	}
	channel.state = COMM_STATE_CONNECTED;
	server.channel = channel;
	id = s_process_state.num_servers;
	s_process_state.num_servers++;
	printf("Server ID %d\n", id);
	s_process_state.servers_state[id] = server;
	return 0;
}

int is_from_server(int sockfd) {
	int i = 0;
	for (i = 0; i < MAX_SERVERS; i++) {
		if (sockfd == s_process_state.servers_state[i].channel.sockfd) {
			printf("Its from Server\n");
			return i;
		}
	}
	return -1;
}

int is_from_client(int sockfd) {
	int i = 0;
	for (i = 0; i < MAX_CLIENTS; i++) {
		if (sockfd == s_process_state.clients_state[i].channel.sockfd) {
			return i;
		}
	}
	return -1;
}

void setState(STATE state) {
	s_process_state.state = state;
}

int sendmessage(ServerMessage message, int sockfd) {
	message.timestamp = s_process_state.timestamp + DIFF;
	s_process_state.timestamp += DIFF;
	int send_result = send(sockfd, &message, sizeof(message), 0);
	if (send_result <= 0) {
		perror("Send failed:");
		return -1;
	}
	return 0;
}

int sendMessage(ServerMessage message, int id, int is_client) {
	printf("TRACE(%d): send command (%d) to %d\n", s_process_state.timestamp, message.command_id, id);
	int sockfd = 0;
	COMM_STATE state = 0;
	if (is_client) {
		sockfd = s_process_state.clients_state[id].channel.sockfd;
		state = s_process_state.clients_state[id].channel.state;
	} else {
		sockfd = s_process_state.servers_state[id].channel.sockfd;
		state = s_process_state.servers_state[id].channel.state;
	}

	if (state == COMM_STATE_CONNECTED || s_process_state.state == STATE_CONTROL_PROCESSING) {
		return sendmessage(message, sockfd);
	} else {
		//TODO RETURN RIGHT ERROR CODE
		printf("TRACE(%d): Send failed to %d state(%d)\n", s_process_state.timestamp, id, state);
		return -1;
	}
}

void setChannelState(int id, COMM_STATE state, int is_client) {
	if (is_client) {
		s_process_state.clients_state[id].channel.state = state;
	} else {
		s_process_state.servers_state[id].channel.state = state;
	}
}

int processWrites(ServerMessage message, int c_id, int is_client) {
	int object_id = message.object_id;
	int object_value = message.object_value;
	int server_id = -1;
	object_st obj = s_process_state.objects[object_id];
	ServerMessage response;
	ServerMessage agreement_message;
	ServerMessage agreement_message1;
	ServerMessage agreement_message2;
	switch(message.command_id) {
	case MESSAGE_WRITE:
		printf("TRACE(%d): obj locked?(%d) by (%d) curr_req(%d)\n", s_process_state.timestamp, obj.locked, obj.current_request.client_id, c_id);
		if (obj.locked == 1) {
			printf("TRACE(%d): object value(%d) to (%d)\n", s_process_state.timestamp, obj.value, object_value);
			obj.temp_value[0] = object_value;
			obj.value = object_value;
			obj.current_operation = MESSAGE_WRITE_RESP;
		} else {
			response.command_id = MESSAGE_WRITE_RESP;
			response.object_id = -1;
			response.object_value = -1;
			sendMessage(response, c_id, is_client);
		}
		// Agrement protocol
		// find H(object_id) H(object_id)+1 H(object_id)+2
		server_id = hash(object_id);
		agreement_message.command_id = MESSAGE_AGREE;
		agreement_message.object_id = obj.id;
		agreement_message.object_value = obj.value;
		agreement_message.client_id = c_id;

		agreement_message1.command_id = MESSAGE_AGREE;
		agreement_message1.object_id = obj.id;
		agreement_message1.object_value = obj.value;
		agreement_message1.client_id = c_id;

		agreement_message2.command_id = MESSAGE_AGREE;
		agreement_message2.object_id = obj.id;
		agreement_message2.object_value = obj.value;
		agreement_message2.client_id = c_id;
		obj.agree_sent = 0;
		// send Agree message with object_id 
		printf("TRACE(%d): Sending agree obj(%d) value(%d)\n", s_process_state.timestamp, object_id, obj.temp_value[0]);
		if (server_id != s_process_state.id) {
			sendMessage(agreement_message, server_id, 0);
			printf("Sent to %d\n", server_id);
			obj.agree_sent += 1;
		}
		if ((server_id+1)%7 != s_process_state.id) {
			sendMessage(agreement_message1, ((server_id+1)%7), 0);
			printf("Sent to %d\n", (server_id+1)%7);
			obj.agree_sent += 1;
		}
		if ((server_id+2)%7 != s_process_state.id) {
			sendMessage(agreement_message2, ((server_id+2)%7), 0);
			printf("Sent to %d\n", (server_id+2)%7);
			obj.agree_sent += 1;
		}

			if (obj.agree_count == obj.agree_sent) {
				obj.agree_count = 0;
				if (obj.temp_value[0] == obj.temp_value[1]) {
					obj.value = obj.temp_value[0];
				}
				obj.temp_value[0] = -1;
				obj.temp_value[1] = -1;
				printf("TRACE(%d): AGREED obj(%d) value(%d) \n", s_process_state.timestamp, object_id, obj.value);
				response.command_id = MESSAGE_WRITE_RESP;
				obj.current_operation = -1;
				response.object_id = obj.id;
				response.object_value = obj.value;
				sendMessage(response, c_id, 1);
				obj.agree_sent = 0;
			}
		break;
	case MESSAGE_UPDATE:
		response.command_id = MESSAGE_UPDATE_RESP;
		if (obj.locked == 1 && obj.current_request.client_id == c_id && obj.in_use == 1) {
			obj.value = object_value;
			obj.temp_value[0] = object_value;
			obj.current_operation = MESSAGE_UPDATE_RESP;
		} else {
			response.object_id = -1;
			response.object_value = -1;
			sendMessage(response, c_id, is_client);
		}
		printf("TRACE(%d): Sending agree obj(%d) value(%d)\n", s_process_state.timestamp, object_id, obj.temp_value[0]);
		server_id = hash(object_id);
		agreement_message.command_id = MESSAGE_AGREE;
		agreement_message.object_id = obj.id;
		agreement_message.object_value = obj.value;
		agreement_message.client_id = c_id;
		agreement_message1.command_id = MESSAGE_AGREE;
		agreement_message1.object_id = obj.id;
		agreement_message1.object_value = obj.value;
		agreement_message1.client_id = c_id;
		agreement_message2.command_id = MESSAGE_AGREE;
		agreement_message2.object_id = obj.id;
		agreement_message2.object_value = obj.value;
		agreement_message2.client_id = c_id;
		obj.agree_sent = 0;
		// send Agree message with object_id 
		if (server_id != s_process_state.id) {
			printf("TRACE(%d): Sending agree server_id(%d) obj(%d) value(%d)\n", server_id, s_process_state.timestamp, object_id, obj.temp_value[0]);
			sendMessage(agreement_message, server_id, 0);
			obj.agree_sent += 1;
		}
		if ((server_id+1)%7 != s_process_state.id) {
			printf("TRACE(%d): Sending agree obj(%d) value(%d)\n", s_process_state.timestamp, object_id, obj.temp_value[0]);
			sendMessage(agreement_message1, (server_id+1)%7, 0);
			obj.agree_sent += 1;
		}
		if ((server_id+2)%7 != s_process_state.id) {
			printf("TRACE(%d): Sending agree obj(%d) value(%d)\n", s_process_state.timestamp, object_id, obj.temp_value[0]);
			sendMessage(agreement_message2, (server_id+2)%7, 0);
			obj.agree_sent += 1;
		}
			if (obj.agree_count == obj.agree_sent) {
				obj.agree_count = 0;
				if (obj.temp_value[0] == obj.temp_value[1]) {
					obj.value = obj.temp_value[0];
				}
				obj.temp_value[0] = -1;
				obj.temp_value[1] = -1;
				printf("TRACE(%d): AGREED obj(%d) value(%d) \n", s_process_state.timestamp, object_id, obj.value);
				response.command_id = MESSAGE_UPDATE_RESP;
				obj.current_operation = -1;
				response.object_id = obj.id;
				response.object_value = obj.value;
				sendMessage(response, obj.current_request.client_id, 1);
				obj.agree_sent = 0;
			}
		break;
	default:
		printf("Unknown update command\n");
	}
	s_process_state.objects[object_id] = obj;
	return 0;
}

int processLocks(ServerMessage message, int c_id, int is_client) {
	int object_id = message.object_id;
	object_st obj = s_process_state.objects[object_id];
	ServerMessage lockResponse;
	int i = 0;
	int timestamp = INT_MAX;
	int req_index = -1;
	switch(message.command_id) {
	case MESSAGE_LOCK:
		// if object not locked, set request to current_request
		// send lock response to client
		if (obj.locked == 0) {
			obj.current_request.client_id = c_id;
			obj.current_request.timestamp = message.seqno;
			printf("TRACE(%d): Current Request no. %d\n", s_process_state.timestamp, obj.current_request.timestamp);
			lockResponse.command_id = MESSAGE_LOCK_RESP;
			lockResponse.object_id = 1;
			lockResponse.object_value = 0;
			sendMessage(lockResponse, c_id, is_client);
			obj.locked = 1;
			obj.in_use = 1;
			obj.id = object_id;
		} else {
			// if object is locked, set the request in request_list
			obj.request_list[obj.request_list_size].timestamp = message.seqno;
			// check if request seq is smaller than current_request
			// if so, send query to current request client
			obj.request_list[obj.request_list_size].client_id = c_id;
			obj.request_list_size++;
			printf("TRACE(%d): Request List:%d\n", s_process_state.timestamp, obj.request_list_size);
			printf("TRACE(%d):Message seq(%d) CurrReq seq(%d)\n", s_process_state.timestamp, message.seqno, obj.current_request.timestamp);
			if (message.seqno < obj.current_request.timestamp) {
				lockResponse.command_id = MESSAGE_QUERY;
				lockResponse.object_id = obj.id;
				lockResponse.object_value = 0;
				sendMessage(lockResponse, obj.current_request.client_id, 1);
			}
			// send failed to request client.
			lockResponse.command_id = MESSAGE_LOCK_RESP;
			lockResponse.object_id = -1;
			lockResponse.object_value = -1;
			sendMessage(lockResponse, c_id, is_client);
		}
		break;
	case MESSAGE_UNLOCK:
		// get the next request with smallest seq in request_list,
		if (obj.current_request.client_id != c_id)
			break;
		printf("TRACE(%d): list size(%d)\n", s_process_state.timestamp, obj.request_list_size);
		lockResponse.command_id = MESSAGE_UNLOCK_RESP;
		lockResponse.object_id = obj.id;
		lockResponse.object_value = obj.value;
		sendMessage(lockResponse, c_id, is_client);
		for (i = 0; i < obj.request_list_size; i++) {
			if (timestamp > obj.request_list[i].timestamp) {
				req_index = i;	
				timestamp = obj.request_list[i].timestamp;
			}
		}
		if (req_index > -1) {
			obj.current_request.client_id = obj.request_list[req_index].client_id;
			obj.current_request.timestamp = obj.request_list[req_index].timestamp;
			for (i = req_index+1; i < obj.request_list_size; i++) {
				obj.request_list[i-1].client_id = obj.request_list[i].client_id;
				obj.request_list[i-1].timestamp = obj.request_list[i].timestamp;
			}
			obj.request_list_size--;
			// send lock if there is request in the request_list
			if (obj.locked == 1) {
				obj.locked = 0;
				if (obj.current_request.client_id == c_id)
					break;
				lockResponse.command_id = MESSAGE_LOCK_RESP;
				lockResponse.object_id = 1;
				lockResponse.object_value = obj.value;
				sendMessage(lockResponse, obj.current_request.client_id, 1);
				obj.locked = 1;
			}
		} else {
			obj.locked = 0;
		}
		break;
	case MESSAGE_QUERY_RESP:
		// move current_request to request_list, get the request with smallest seq in request_list
		obj.request_list[obj.request_list_size].client_id = obj.current_request.client_id;
		obj.request_list[obj.request_list_size].timestamp = obj.current_request.timestamp;
		obj.request_list_size++;

		for (i = 0; i < obj.request_list_size; i++) {
			if (timestamp > obj.request_list[i].timestamp) {
				req_index = i;
				timestamp = obj.request_list[i].timestamp;
			}
		}
		if (req_index > -1) {
			// set current_request to request with smallest seq
			obj.current_request.client_id = obj.request_list[req_index].client_id;
			obj.current_request.timestamp = obj.request_list[req_index].timestamp;
			for (i = req_index+1; i < obj.request_list_size; i++) {
				obj.request_list[i-1].client_id = obj.request_list[i].client_id;
				obj.request_list[i-1].timestamp = obj.request_list[i].timestamp;
			}
			obj.request_list_size--;
			// send lock success to the request with smallest seq
			lockResponse.command_id = MESSAGE_LOCK_RESP;
			lockResponse.object_id = 1;
			lockResponse.object_value = obj.value;
			sendMessage(lockResponse, obj.current_request.client_id, 1);
		}
		break;
	case MESSAGE_AGREE:
		printf("TRACE(%d): AGREE seqno(%d) obj value(%d)\n", s_process_state.timestamp, message.seqno, message.object_value); 
		{
			obj.temp_value[obj.agree_count] = message.object_value;
			obj.agree_count+=1;

			obj.in_use = 1;
			if (obj.locked == 0 || (obj.locked == 1 && message.client_id != obj.current_request.client_id)) {
				lockResponse.command_id = MESSAGE_AGREE;
				lockResponse.object_id = obj.id;
				lockResponse.object_value = message.object_value;
				lockResponse.seqno = message.seqno;
				obj.value = message.object_value;
				obj.agree_count = 0;
				obj.agree_sent = 0;
				lockResponse.client_id = message.client_id;
				sendMessage(lockResponse, c_id, 0);
			}

			if (obj.agree_count == obj.agree_sent && obj.locked == 1) {
				obj.agree_count = 0;
				if (obj.agree_sent <= 0) {
					// Wait for client write operation.
					break;
				}
				if (obj.temp_value[0] == obj.temp_value[1]) {
					obj.value = obj.temp_value[0];
				}
				obj.temp_value[0] = -1;
				obj.temp_value[1] = -1;
				printf("TRACE(%d): AGREED obj(%d) value(%d) \n", s_process_state.timestamp, object_id, obj.value);
				lockResponse.command_id = obj.current_operation;
				lockResponse.object_id = obj.id;
				lockResponse.object_value = obj.value;
				lockResponse.client_id = message.client_id;
				sendMessage(lockResponse, obj.current_request.client_id, 1);
				obj.current_operation = -1;
				obj.agree_sent = 0;
			}
		}
		break;
	default:
		printf("Unknown command\n");
	}
	s_process_state.objects[object_id] = obj;
	return 0;
}

int processControlMessages(int command, int c_id, int is_client) {
	printf("TRACE(%d): command(%d) c_id(%d) isClient(%d)\n", s_process_state.timestamp, command, c_id, is_client);
	ServerMessage message;
	int send_result = -1;
	switch(command) {
	case MESSAGE_SUSPEND:
		setState(STATE_CONTROL_PROCESSING);
		message.command_id = MESSAGE_SUSPEND_RESP;
		message.object_id = 0;
		message.object_value = 0;
		send_result = sendMessage(message, c_id, is_client);
		setChannelState(c_id, COMM_STATE_SUSPENDED, is_client);
		setState(STATE_INITIALIZED);
		break;
	case MESSAGE_RESUME:
		setState(STATE_CONTROL_PROCESSING);
		message.command_id = MESSAGE_RESUME_RESP;
		message.object_id = 0;
		message.object_value = 0;
		send_result = sendMessage(message, c_id, is_client);
		setChannelState(c_id, COMM_STATE_CONNECTED, is_client);
		setState(STATE_INITIALIZED);
		break;
	case MESSAGE_SUSPEND_RESP:
	case MESSAGE_RESUME_RESP:
		printf("Control Response arrived\n");
		break;
	default:
		printf("Invalid command in control processing %d\n", command);
	}
	return send_result;
}

int processRead(int command, int object_id, int id, int isClientid) {
	ServerMessage message;
	setState(STATE_READING);
	message.command_id = MESSAGE_READ_RESP;
	if (object_id < 100 && object_id > -1) {
		object_st obj = s_process_state.objects[object_id];
		if (obj.in_use != 0) {
			message.object_id = object_id;
			message.object_value = obj.value;
		} else {
			//TODO define errors
			message.object_id = -1;
			message.object_value = -1;
		}
	} else {
		//TODO define errors
		message.object_id = -1;
		message.object_value = -2;
	}
	printf("TRACE(%d): Object id(%d) value(%d)\n", s_process_state.timestamp, message.object_id, message.object_value);
	return sendMessage(message, id, isClientid);
}

int processMessage(ServerMessage message, int serv_id, int client_id) {
	printf("TRACE(%d): command: %d serv_id: %d client_id:%d\n", s_process_state.timestamp, message.command_id, serv_id, client_id);
	int isClientid = (serv_id != -1)?0:1;
	int id = (serv_id != -1)?serv_id:client_id;
	s_process_state.timestamp = (s_process_state.timestamp > message.timestamp)?s_process_state.timestamp:message.timestamp;
	s_process_state.timestamp += DIFF;
	if (message.command_id == MESSAGE_SUSPEND ||
		message.command_id == MESSAGE_RESUME ||
		message.command_id == MESSAGE_SUSPEND_RESP ||
		message.command_id == MESSAGE_RESUME_RESP) {
		// process control message
		return processControlMessages(message.command_id, id, isClientid);
	} else if (message.command_id == MESSAGE_WRITE || message.command_id == MESSAGE_UPDATE) {
		// process write or update
		return processWrites(message, id, isClientid);
	} else if (message.command_id == MESSAGE_READ) {
		// process read and send response
		return processRead(message.command_id, message.object_id, id, isClientid);
	} else if (message.command_id < MESSAGE_TYPE_MAX) {
		// maekawa algorithm
		processLocks(message, id, isClientid);
	} else {
		printf("Bad message command id\n");
	}
	return 0;
}

int processReceive(int sockfd) {
	ServerMessage message;
	int server_index = is_from_server(sockfd);
	int client_index = -1;
	if (server_index == -1) {
		client_index = is_from_client(sockfd);
	}
	printf("TRACE(%d): s_id(%d) c_id(%d)\n", s_process_state.timestamp, server_index, client_index);
	int recv_size = recv(sockfd, &message, sizeof(message), 0);
	if (recv_size <= 0) {
		printf("Receive failed\n");
		if (server_index > -1) {
			s_process_state.servers_state[server_index].channel.state = COMM_STATE_INITIALIZED;
			s_process_state.servers_state[server_index].channel.sockfd = -1;
			s_process_state.num_servers--;
		} else {
			s_process_state.clients_state[client_index].channel.state = COMM_STATE_INITIALIZED;
			s_process_state.clients_state[client_index].channel.sockfd = -1;
			s_process_state.num_clients--;
		}
		return -1;
	} else {
		processMessage(message, server_index, client_index);
		return 0;
	}
}

void processInput(int inputfd) {
	char buf[MAX_BUFSIZE] = {'\0',};
	s_process_state.timestamp += DIFF;
	int read_size = read(inputfd, buf, MAX_BUFSIZE);
	if (read_size < 0) {
		return;
	} else {
		printf("Read: %s\n", buf);
	}
}

void start() {
	fd_set fds;
	fd_set localfds;
	int index = 0;
	int maxfd = 0;

	FD_ZERO(&fds);
	FD_ZERO(&localfds);
	// other server sockets
	for (index = 0; index < MAX_SERVERS; index++) {
		if (s_process_state.servers_state[index].channel.state == COMM_STATE_CONNECTED) {
			printf("Adding %d to fds\n", s_process_state.servers_state[index].channel.sockfd);
			FD_SET(s_process_state.servers_state[index].channel.sockfd, &fds);
			if (maxfd < s_process_state.servers_state[index].channel.sockfd) {
				maxfd = s_process_state.servers_state[index].channel.sockfd;
			}
		}
	}

	// input socket
	s_process_state.inputfd = STDIN_FILENO;
	printf("Adding input(%d) to fds\n", s_process_state.inputfd);
	FD_SET(s_process_state.inputfd, &fds);
	// server socket
	printf("Adding %d to fds\n", s_process_state.server_state.sockfd);
	FD_SET(s_process_state.server_state.sockfd, &fds);
	if (maxfd < s_process_state.server_state.sockfd) {
		maxfd = s_process_state.server_state.sockfd;
	}

	int done = 0;
	while (!done) {
		FD_ZERO(&localfds);
		localfds = fds;
		fflush(NULL);
		int result = select(maxfd+1, &localfds, NULL, NULL, NULL);
		if (result < 0) {
			perror("Select failed:");
			continue;
		} else {
			// process fds
			int i = 0;
			for (i = 0; i <= maxfd; i++) {
				if (FD_ISSET(i, &localfds)) {
				if (i == s_process_state.inputfd) {
					// process input data
					printf("Input fd set!\n");
					processInput(i);
				} else if (i == s_process_state.server_state.sockfd) {
					// new incoming connection?
					struct sockaddr_in addr;
					unsigned int addrlen = sizeof(addr);
					int sockfd = accept(i, (struct sockaddr *)&addr, &addrlen);
					if (sockfd < 0) {
						perror("Socket accept failed: ");
					} else {
						int port = ntohs(addr.sin_port);
						if (port < CLIENT_PORT) {
							// from other server
							int id = s_process_state.num_servers++;
							if (id == s_process_state.id)
								id = s_process_state.num_servers++;
							printf("Connection request from %s %u server id %d\n", inet_ntoa(addr.sin_addr), port, id);
							s_process_state.servers_state[id].server_id = port - SERVER_PORT -1;
							s_process_state.servers_state[id].channel.sockfd = sockfd;
							s_process_state.servers_state[id].channel.state = COMM_STATE_CONNECTED;
							memcpy(&s_process_state.servers_state[id].channel.addr, &addr, sizeof(struct sockaddr_in));
						} else {
							// connection from client;
							int id = s_process_state.num_clients++;
							printf("Connection request from %s %u client id %d\n", inet_ntoa(addr.sin_addr), port, id);
							s_process_state.clients_state[id].client_id = port - CLIENT_PORT -1;
							s_process_state.clients_state[id].channel.sockfd = sockfd;
							s_process_state.clients_state[id].channel.state = COMM_STATE_CONNECTED;
							memcpy(&(s_process_state.clients_state[id].channel.addr), &addr, sizeof(struct sockaddr_in));
						}
						FD_SET(sockfd, &fds);
						if (maxfd < sockfd) {
							maxfd = sockfd;
						}
					}
				} else {
					// data from other server or client
					if (processReceive(i) < 0) {
						close(i);
						FD_CLR(i, &fds);
						if (maxfd == i)
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
		printf("Usage: server <id>\n");
		return -1;
	}

	// initialize global states
	int id = atoi(argv[1]);
	if (initialize(id) < 0) {
		printf("Init failed!\n");
		return -1;
	}
	printf("Server socket = %d\n", s_process_state.server_state.sockfd);
	int i = 0;
	for (i = 0; i < id; i++) {
		connect_to_server(i);
	}

	start();
	return 0;
}
