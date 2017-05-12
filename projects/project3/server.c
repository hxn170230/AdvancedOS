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
#include "server.h"

static server_st server;

int get_server(int sockfd) {
	int i = 0;
	for (i = 0; i < MAX_SERVERS; i++) {
		if (sockfd == server.servers[i].ch_info.fd) {
			return i;
		}
	}
	return -1;
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

int get_lowest_connected_server_id() {
	int i = 0;
	for (i = 0; i < MAX_SERVERS; i++) {
		if (server.servers[i].ch_info.state == CHANNEL_STATE_CONNECTED) {
			return i;
		}
	}
	return -1;
}

int get_id(int hv) {
	int i = 0;
	for (i = 0; i < MAX_SERVERS; i++) {
		if (server.servers[i].ch_info.state == CHANNEL_STATE_CONNECTED && hv == server.servers[i].object.version) {
			return i;
		}
	}
	return -1;
}

int get_highest_version() {
	int i = 0;
	int h_v = 0;
	for (i = 0; i < MAX_SERVERS; i++) {
		if (server.servers[i].ch_info.state == CHANNEL_STATE_CONNECTED) {
			if (server.servers[i].object.version > h_v) {
				h_v = server.servers[i].object.version;
			}
		}
	}
	return h_v;
}

int get_highest_version_votes() {
	int i = 0;
	int highest_version = get_highest_version();
	printf("Latest version: %d\n", highest_version);
	int num_votes = 0;
	for (i = 0; i < MAX_SERVERS; i++) {
		if (server.servers[i].ch_info.state == CHANNEL_STATE_CONNECTED) {
			printf("Server %d object version:  %d\n", i, server.servers[i].object.version);
			if (server.servers[i].object.version == highest_version) {
				num_votes += 1;
			}
		}
	}
	printf("Votes obtained: %d\n", num_votes);
	return num_votes;
}

void dump_message(server_message message, int sockfd, char *event) {
	int id = get_server(sockfd);
	switch (message.m) {
	case VOTE_REQ:
		printf("%s VOTE_REQ %d\n", event, id);
		break;
	case VOTE_RESP:
		printf("%s VOTE_RESP %d\n", event, id);
		printf("OBJECT: VAL(%d) VER(%d) RU(%d) DS(%d)\n", message.object.value, message.object.version, message.object.ru, message.object.ds);
		break;
	case WRITE_REQ:
		printf("%s WRITE_REQ  %d\n", event, id);
		printf("OBJECT: VAL(%d) VER(%d) RU(%d) DS(%d)\n", message.object.value, message.object.version, message.object.ru, message.object.ds);
		break;
	case WRITE_RESP:
		printf("%s WRITE_RESP  %d\n", event, id);
		break;
	case READ_REQ:
		printf("%s READ_REQ  %d\n", event, id);
		break;
	case READ_RESP:
		printf("%s READ_RESP  %d\n", event, id);
		printf("OBJECT: VAL(%d) VER(%d) RU(%d) DS(%d)\n", message.object.value, message.object.version, message.object.ru, message.object.ds);
		break;
	case UPDATE_REQ:
		printf("%s UPDATE_REQ  %d\n", event, id);
		printf("OBJECT: VAL(%d) VER(%d) RU(%d) DS(%d)\n", message.object.value, message.object.version, message.object.ru, message.object.ds);
		break;
	case UPDATE_RESP:
		printf("%s UPDATE_RESP  %d\n", event, id);
		break;
	default:
		printf("Unknown message  %d\n", id);
	}
}

void initialize(int id) {
	server.id = id;
	server.num_votes_req = 0;
	server.num_votes_resp = 0;
	server.num_write_requests = 0;
	server.num_writes = 0;
	server.write_pending = 0;
	int i = 0;
	for (i = 0; i < MAX_SERVERS; i++) {
		memset(&(server.servers[i]), 0, sizeof(server_info));
		server.servers[i].id = i;
		server.servers[i].ch_info.fd = -1;
		server.servers[i].ch_info.state = CHANNEL_STATE_INITED;
		server.servers[i].object.id = 0;
		server.servers[i].object.value = 0;
		server.servers[i].object.version = 1;
		server.servers[i].object.ru = 8;
		server.servers[i].object.ds = 0;
	}
	server_info current_server = server.servers[id];
	current_server.ch_info.fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (current_server.ch_info.fd < 0) {
		perror("Socket init failed");
		return;
	}

	char *server_name = servers_names[id];
	get_server_info(server_name, &current_server.ch_info);
	int reuse_addr = 1;
	if (setsockopt(current_server.ch_info.fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0) {
		perror("SET SOCK OPT Failed");
	}
	if (bind(current_server.ch_info.fd, (struct sockaddr *)&current_server.ch_info.addr, sizeof(struct sockaddr_in)) < 0) {
		perror("Server bind failed");
		close(current_server.ch_info.fd);
		current_server.ch_info.fd = -1;
		current_server.ch_info.state = CHANNEL_STATE_INITED;
		return;
	}

	current_server.ch_info.state = CHANNEL_STATE_CONNECTED;
	if (listen(current_server.ch_info.fd, BACKLOG) < 0) {
		perror("Server listen failed");
		close(current_server.ch_info.fd);
		current_server.ch_info.fd = -1;
		current_server.ch_info.state = CHANNEL_STATE_INITED;
	}
	memcpy(&server.servers[id], &current_server, sizeof(server_info));
}

void connect_to_server(int id) {
	char *server_name = servers_names[id];
	get_server_info(server_name, &(server.servers[id].ch_info));
	server.servers[id].ch_info.state = CHANNEL_STATE_CONNECTING;
	server.servers[id].ch_info.fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server.servers[id].ch_info.fd < 0) {
		perror("Socket init failed");
		return;
	}

	if (connect(server.servers[id].ch_info.fd, (struct sockaddr *)&(server.servers[id].ch_info.addr), sizeof(struct sockaddr)) < 0) {
		perror("Connect failed");
		close(server.servers[id].ch_info.fd);
		server.servers[id].ch_info.state = CHANNEL_STATE_INITED;
		server.servers[id].ch_info.fd = -1;
		return;
	}
	printf("Connected to %d socket %d\n", id, server.servers[id].ch_info.fd);
	server.servers[id].ch_info.state = CHANNEL_STATE_CONNECTED;
}

int get_host_id(struct sockaddr_in addr) {
	socklen_t len = sizeof(struct sockaddr_in);
	char hbuf[NI_MAXHOST];

	printf("Trying to get host name\n");
	if (!getnameinfo((struct sockaddr *)&addr, len, hbuf, sizeof(hbuf), NULL, 0, NI_NAMEREQD)) {
		int i = 0;
		printf("Got host name: %s\n", hbuf);
		while (i < MAX_SERVERS) {
			if (!strncmp(hbuf, servers_names[i], strlen(servers_names[i]))) {
				printf("Connection from server %d\n", i);
				return i;
			}
			i++;
		}
	}
	return -1;
}

void dump_object(object_info *object) {
	printf("OBJECT VALUE: %d\n", object->value);
	printf("OBJECT VERSION: %d\n", object->version);
	printf("OBJECT RU: %d\n", object->ru);
	printf("OBJECT DS: %d\n", object->ds);
}

int send_message(server_message message, int sockfd) {
	server_message smessage;
	memcpy(&smessage, &message, sizeof(server_message));
	dump_message(message, sockfd, "SENDING");
	int send_result = send(sockfd, &smessage, sizeof(server_message), 0);
	if (send_result < sizeof(server_message)) {
		perror("Send failed");
		return -1;
	}
	return 0;
}

void update_and_write(int valid_votes, int ds) {
	int hv = get_highest_version();
	server.servers[server.id].object.ru = valid_votes;
	if (ds == -1)
		server.servers[server.id].object.ds = get_lowest_connected_server_id();
	else {
		server.servers[server.id].object.ds = ds;
	}
	if (hv == server.servers[server.id].object.version) {
		// version is the highest
		server.servers[server.id].object.version += 1;
		server.servers[server.id].object.value = server.write_object.value;
		// update other sites
		server_message smessage;
		smessage.m = WRITE_REQ;
		memcpy(&smessage.object, &server.servers[server.id].object, sizeof(object_info));
		int i = 0;
		for (i = 0; i < MAX_SERVERS; i++) {
			if (i != server.id && server.servers[i].ch_info.state == CHANNEL_STATE_CONNECTED) {
				send_message(smessage, server.servers[i].ch_info.fd);
				server.num_writes += 1;
			}
		}
	} else {
		int id = get_id(hv);
		server_message read_message;
		read_message.m = READ_REQ;
		memcpy(&read_message.object, &server.servers[server.id].object, sizeof(object_info));
		send_message(read_message, server.servers[id].ch_info.fd);
	}
}

void process_write_req(int object_id, int object_value);

void reset() {
	server.write_pending = 0;
	server.num_votes_req = 0;
	server.num_votes_resp = 0;
	server.num_writes = 0;
	server.num_write_requests -= 1;
	sleep(1);
	if (server.num_write_requests > 0) {
		process_write_req(server.write_object.id, server.write_object.value);
	}
}

void process_write_req(int object_id, int object_value) {
	server_message message;
	message.m = VOTE_REQ;
	server.write_object.id = object_id;
	server.write_object.value = object_value;
	server.write_pending = 1;
	memcpy(&message.object, &server.write_object, sizeof(object_info));

	int i = 0;
	for (i = 0; i < MAX_SERVERS; i++) {
		if (i != server.id && server.servers[i].ch_info.state == CHANNEL_STATE_CONNECTED) {
			if (send_message(message, server.servers[i].ch_info.fd) == 0) {
				server.num_votes_req += 1;
			}
		}
		if (i == server.id) {
			server.num_votes_req += 1;
			server.num_votes_resp += 1;
		}
	}

	if (server.num_votes_req == 1) {
		// all servers disconnected
		if (2*(server.num_votes_req) > server.servers[server.id].object.ru) {
			update_and_write(1, -1);
		} else if (2*(server.num_votes_req) == server.servers[server.id].object.ru){
			if (server.id == server.servers[server.id].object.ds) {
				update_and_write(1, server.servers[server.id].object.ds);
			} else {
				printf("VOTING TIE BREAKER FAILED\n");
				reset();
			}
		} else {
			printf("VOTING FAILED\n");
			reset();
		}
	}
}

int process_input(int fd, int *add_delete) {
	char buf[100] = {'\0',};
	int read_size = read(fd, buf, 100);

	if (read_size > 0) {
		printf("READ FROM INPUT: %s\n", buf);
		char *token1 = strtok(buf, " ");
		if (token1[strlen(token1)-1] == '\n') {
			token1[strlen(token1)-1] = '\0';
		}
		if (token1 != NULL) {
			if (!strcmp(token1, "WRITE")) {
				char *token2 = strtok(NULL, " ");
				char *token3 = strtok(NULL, " ");
				char *token4 = strtok(NULL, " ");
				if (token4 != NULL) {
					server.num_write_requests = atoi(token4);
				}
				int object_id = atoi(token2);
				int object_value = atoi(token3);
				process_write_req(object_id, object_value);
		
			} else if (!strcmp(token1, "READ")) {
				printf("OBJECT: %d\n", server.servers[server.id].object.value);
				printf("OBJECT VN: %d\n", server.servers[server.id].object.version);
				printf("OBJECT RU: %d\n", server.servers[server.id].object.ru);
				printf("OBJECT DS: %d\n", server.servers[server.id].object.ds);
			} else if (!strcmp(token1, "CONNECT")) {
				char *token2 = strtok(NULL, " ");
				if (token2[strlen(token2)-1] == '\n') {
					token2[strlen(token2)-1] = '\0';
				}
				int id = atoi(token2);
				if (server.servers[id].ch_info.state == CHANNEL_STATE_INITED) {
					connect_to_server(id);
					*(add_delete) = 1;
					return server.servers[id].ch_info.fd;
				}
			} else if (!strcmp(token1, "DISCONNECT")) {
				char *token2 = strtok(NULL, " ");
				if (token2[strlen(token2)-1] == '\n') {
					token2[strlen(token2)-1] = '\0';
				}
				int id = atoi(token2);
				if (server.servers[id].ch_info.state == CHANNEL_STATE_CONNECTED) {
					close(server.servers[id].ch_info.fd);
					server.servers[id].ch_info.state = CHANNEL_STATE_INITED;
					*(add_delete) = 0;
					int ret = server.servers[id].ch_info.fd;
					server.servers[id].ch_info.fd = -1;
					return ret;
				}
			}
		}
	}
	return 0;
}

void check_and_write() {
	int valid_votes = get_highest_version_votes();
	int hv = get_highest_version();
	int id = get_id(hv);
	printf("ID %d latest version: %d\n", id, hv);
	object_info object = server.servers[id].object;
	if (2*valid_votes > (object.ru)) {
		// majority
		update_and_write(valid_votes, -1);
	} else if (2*valid_votes == (object.ru)) {
		// tie
		if (server.servers[object.ds].ch_info.state == CHANNEL_STATE_CONNECTED) {
			// tie breaker majority
			update_and_write(valid_votes, object.ds);
		} else {
			// tie breaker loss
			printf("VOTING TIE BREAKER FAILED\n");
			reset();
		}
	} else {
		// not enough majority
		printf("VOTING FAILED\n");
		reset();
	}
}

int process_receive(int fd) {
	server_message recv_message;
	int recv_size = recv(fd, &recv_message, sizeof(recv_message), 0);
	if (recv_size <= 0) {
		return -1;
	}
	int id = get_server(fd);
	dump_message(recv_message, fd, "RECEIVED");
	if (id < 0) return 1;
	printf("Received %d from %d\n", recv_message.m, id);

	server_message smessage;
	server_message write_message;
	server_message rmessage;
	server_message write_response_message;
	object_info object;

	switch (recv_message.m) {
	case VOTE_REQ:
		smessage.m = VOTE_RESP;
		memcpy(&smessage.object, &server.servers[server.id].object, sizeof(object_info));
		if (send_message(smessage, fd)) {
			printf("Send failed to %d\n", id);
		}
		break;
	case VOTE_RESP:
		memcpy(&server.servers[id].object, &recv_message.object, sizeof(object_info));
		server.num_votes_resp += 1;
		if (server.num_votes_resp == server.num_votes_req && server.num_votes_req != 0) {
			printf("All responses arrived. Run voting algorithm\n");
			check_and_write();
		}
		break;
	case READ_REQ:
		rmessage.m = READ_RESP;
		memcpy(&rmessage.object, &server.servers[server.id].object, sizeof(object_info));
		if (send_message(rmessage, fd)) {
			printf("Read Response failed\n");
		}
		break;
	case READ_RESP:
		object = recv_message.object;
		if (server.write_pending == 1) {
			// got updated object
			server.servers[server.id].object.value = object.value;
			server.servers[server.id].object.version = object.version;
			// ru and ds are already updated.
			// update to latest object now
			server.servers[server.id].object.value = server.write_object.value;
			server.servers[server.id].object.version += 1;
			server.servers[server.id].object.ru = server.num_votes_resp;
			int i = 0;
			write_message.m = WRITE_REQ;
			dump_object(&server.servers[server.id].object);
			memcpy(&write_message.object, &server.servers[server.id].object, sizeof(object_info));
			dump_object(&write_message.object);
			for (i = 0; i < MAX_SERVERS; i++) {
				if (i != server.id && server.servers[i].ch_info.state == CHANNEL_STATE_CONNECTED) {
					send_message(write_message, server.servers[i].ch_info.fd);
					server.num_writes+=1;
				}
			}
		}
		break;
	case WRITE_REQ:
	case UPDATE_REQ:
		object = recv_message.object;
		dump_object(&object);
		memcpy(&server.servers[server.id].object, &object, sizeof(object_info));
		dump_object(&server.servers[server.id].object);
		write_response_message.m = WRITE_RESP;
		memcpy(&write_response_message.object, &server.servers[server.id].object, sizeof(object_info));
		send_message(write_response_message, fd);
		break;
	case WRITE_RESP:
	case UPDATE_RESP:
		printf("Received WRITE/UPDATE RESP from %d\n", id);
		server.num_writes-=1;
		if (server.num_writes == 0)
			reset();
		break;
	default:
		printf("Unknown message\n");
	}
	return 1;
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
		if (server.servers[index].ch_info.state == CHANNEL_STATE_CONNECTED) {
			printf("Adding %d to fds\n", server.servers[index].ch_info.fd);
			FD_SET(server.servers[index].ch_info.fd, &fds);
			if (maxfd < server.servers[index].ch_info.fd) {
				maxfd = server.servers[index].ch_info.fd;
			}
		}
	}
	FD_SET(STDIN_FILENO, &fds);

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
				if (i == STDIN_FILENO) {
					// process input data
					printf("Input fd set!\n");
					int add_delete = 0;
					int fd = process_input(i, &add_delete);
					if (fd > 0) {
						if (add_delete == 0) {
							printf("DISCONNECTED SOCKET %d\n", fd);
							FD_CLR(fd, &fds);
							if (maxfd == fd)
								maxfd--;
						} else {
							printf("CONNECTED SOCKET %d\n", fd);
							FD_SET(fd, &fds);
							if (maxfd < fd) {
								maxfd = fd;
							}
						}
					}
				} else if (i == server.servers[server.id].ch_info.fd) {
					// new incoming connection?
					struct sockaddr_in addr;
					unsigned int addrlen = sizeof(addr);
					int sockfd = accept(i, (struct sockaddr *)&addr, &addrlen);
					printf("New connection request socket id %d\n", sockfd);
					if (sockfd < 0) {
						perror("Socket accept failed: ");
					} else {
						int id = get_host_id(addr);
						if (id < 0) {
							perror("Invalid ID");
							close(sockfd);
						} else {
							printf("Connection request accepted from %s server id %d\n", inet_ntoa(addr.sin_addr), id);
							server.servers[id].ch_info.state = CHANNEL_STATE_CONNECTED;
							server.servers[id].ch_info.fd = sockfd;
							memcpy(&server.servers[id].ch_info.addr, &addr, sizeof(struct sockaddr_in));
							FD_SET(sockfd, &fds);
							if (maxfd < sockfd) {
								maxfd = sockfd;
							}
						}
					}
				} else {
					// data from other server or client
					if (process_receive(i) < 0) {
						close(i);
						int id = get_server(i);
						if (id >= 0) {
							server.servers[id].ch_info.state = CHANNEL_STATE_INITED;
							server.servers[id].ch_info.fd = -1;
						}
						printf("SOCKET %d CLOSED SERVER %d\n", i, id);
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

int main (int argc, char *argv[]) {
	if (argc < 2) {
		printf("Usage: <server> <ID>\n");
		return 0;
	}

	int id = atoi(argv[1]);
	initialize(id);

	int i = 0;
	for (i = 0; i < id; i++) {
		connect_to_server(i);
	}

	start();
	return 0;
}
