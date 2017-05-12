#ifndef OBJECT_H
#define OBJECT_H

#define OUTSTANDING_REQUESTS_MAX 6

typedef struct {
	int timestamp;
	int client_id;
}request;

typedef struct {
	int id;
	int locked;
	int value;
	int request_list_size;
	request request_list[OUTSTANDING_REQUESTS_MAX];
	request current_request;
	MESSAGE_TYPES current_operation;
	int in_use;
	int temp_value[2];
	int agree_sent;
	int agree_count;
}object_st;

#endif
