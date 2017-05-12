/*
 * AUTHOR: HARSHAVARDHAN NALAJALA
 * UTD ID: 2021346835
 * EMAIL ID: hxn170230@utdallas.edu
 */

#include "conf.h"

/* global process state */
static process_state globalState;

/* critical section entrance. Writes to rw.txt and OUTPUT<processid>.txt and returns */
void enterCriticalSection() {
    time_t rawtime;
    struct tm * timeinfo;
    time (&rawtime);
    timeinfo = localtime(&rawtime);
    FILE *fp = fopen("rw.txt", "a");

    char buffer[100] = {0,};
    sprintf(buffer, "OUTPUT%d.txt", (globalState.number-1));
    FILE *outputFile = fopen(buffer, "a");

    struct timeval local;
    gettimeofday(&local, NULL);
    long elapsedTime = (local.tv_sec - globalState.upperState.timeOfRequest.tv_sec) *1000000 + (local.tv_usec - globalState.upperState.timeOfRequest.tv_usec);

    if (outputFile == NULL) {
        printf("Error opening output file..print to screen\n");
    } else {
        memset(buffer, 0, 100);
        sprintf(buffer, "(%d): Messages Exchanged: %d\n", globalState.time, globalState.upperState.messagesSent + globalState.upperState.messagesReceived);
        fwrite(buffer, 1, strlen(buffer), outputFile);
        memset(buffer, 0, 100);
        sprintf(buffer, "(%d): Latency: %ldms\n\n", globalState.time, elapsedTime/1000);
        fwrite(buffer, 1, strlen(buffer), outputFile);
        fclose(outputFile);
    }

    printf("(%d): Messages Exchanged: %d\n", globalState.time, globalState.upperState.messagesSent + globalState.upperState.messagesReceived);
    globalState.upperState.messagesSent = 0;
    globalState.upperState.messagesReceived = 0;

    printf("(%d) LATENCY: %ldms\n", globalState.time, elapsedTime/1000);
    memset(&(globalState.upperState.timeOfRequest), 0, sizeof(struct timeval));

    if (fp != NULL) {
        memset(buffer, 0, 100);
        sprintf(buffer, "Entering %d %s\n", globalState.number, asctime(timeinfo));
        fwrite(buffer, 1, strlen(buffer), fp);
        sleep(3);
        // increment time to account for critical section
        globalState.time = globalState.time + 3*globalState.diff;
        fclose(fp);
    }
}

/* Function to send requests to other processes connected to it. If token is with self, no request is sent out
 * Returns 0 if token is available
 * Returns 1 if token is not available and requests are sent to other processes
 */
int requestMutualExclusion() {
    gettimeofday(&(globalState.upperState.timeOfRequest), NULL);
    // increment time to account for timer
    if (globalState.time < globalState.upperState.timerStart + globalState.upperState.timerValue) {
        globalState.time = globalState.upperState.timerStart + globalState.upperState.timerValue;
    }
    // increment time to account for send event
    globalState.time += globalState.diff;
    if (globalState.upperState.haveToken == 1) {
        // have token!
        globalState.upperState.requestTimeStamp = globalState.time;
        return 0;
    } else {
        // donot have token
        // send messages to all outgoingSockets
        int i = 0;
        int bytesSent = 0;

        message buffer;
        buffer.type = REQUEST_CRITICAL_SECTION;
        buffer.timestamp = globalState.time;
        globalState.upperState.requestTimeStamp = globalState.time;


        for (i = 0; i < N_PROCESSES; i++) {
            if (i != globalState.number-1) {
                if (globalState.channels[i].outgoingState == CONNECTED) {
                    globalState.upperState.messagesExchanged++;
                    globalState.upperState.messagesSent++;
                    globalState.upperState.totalMessagesSent++;
                    bytesSent = send(globalState.channels[i].outgoingSocket, (void *)&buffer, sizeof(message), 0);
                    if (bytesSent != sizeof(message)) {
                        printf("(%d): Request send failed!", globalState.time);
                    }
                }
            }
        }
        return 1;
    }
}

/* Function to return outgoing socket connection */
int getSendSocketByProcessId(int processId) {
    return globalState.channels[processId-1].outgoingSocket;
}

/* Function to get the process id based on the socket connection identifier */
int getProcessId(int socket) {
    int i = 0;
    for (i = 0; i < N_PROCESSES; i++) {
        if (globalState.channels[i].incomingSocket == socket ||
            globalState.channels[i].outgoingSocket == socket) {
            return i+1;
        }
    }
    return -1;
}

/* Function to add process ids of requests deferred for later replies */
void addToDeferredList(int processId) {
    globalState.deferredList[globalState.deferredCount] = processId;
    globalState.deferredCount++;
}

/* Function to send replies to all the deferred processes */
void sendRepliesToDeferredList() {
    message buffer;
    buffer.type = REPLY_CRITICAL_SECTION;
    buffer.timestamp = globalState.time;
    int i = 0;
    int bytesSent = 0;

    for (i = 0; i < globalState.deferredCount; i++) {
        int socket = globalState.channels[globalState.deferredList[i]-1].outgoingSocket;
        globalState.upperState.messagesExchanged++;
        globalState.upperState.totalMessagesSent++;
        bytesSent = send(socket, (void *)&buffer, sizeof(message), 0);
        if (bytesSent != sizeof(message)) {
            perror("Reply failure \n");
        }
        globalState.deferredList[i] = -1;
        globalState.upperState.haveToken = 0;
    }
    globalState.deferredCount = 0;
}

/* Function to process computation end. Closes all socket connections and exits */
void processComputationEnd() {
    int i = 0;
    if (globalState.computation_end < N_PROCESSES) {
        return;
    }
    for (i = 0; i < N_PROCESSES; i++) {
        if (globalState.channels[i].outgoingState == CONNECTED) {
            close(globalState.channels[i].outgoingSocket);
            globalState.channels[i].outgoingState = DISCONNECTED;
            close(globalState.channels[i].incomingSocket);
            globalState.channels[i].incomingState = DISCONNECTED;
        }
    }
}

/* Function to run the mutual exclusion algorithm (Ricart-Agarwala Algorithm here)
 * Returns 0 if no message needs to be sent
 * Returns 1 if message needs to be sent(sendBuffer)
 */
int upperLayer(message buffer, message *sendBuffer, int processId, struct timeval **tv) {
    // increment time to account for receive event
    globalState.time = (globalState.time > buffer.timestamp)?globalState.time:buffer.timestamp;
    globalState.time = globalState.time + globalState.diff;
    globalState.upperState.messagesExchanged++;
    globalState.upperState.totalMessagesReceived++;

    if (buffer.type == COMPUTATION_END) {
        globalState.computation_end++;
        if (globalState.computation_end == N_PROCESSES && globalState.number == ROOT_NODE+1) {
            processComputationEnd();
            return 0;
        }
        return 0;
    }

    if (buffer.type == REQUEST_CRITICAL_SECTION) {
        if (globalState.upperState.requestTimeStamp != -1) {
            if ((globalState.upperState.requestTimeStamp < buffer.timestamp) ||
                ((globalState.upperState.requestTimeStamp == buffer.timestamp) &&
                (globalState.number < processId))) {
                // donot send reply
                // add processId to deferredList
                addToDeferredList(processId);
                return 0;
            } else {
                sendBuffer->type = REPLY_CRITICAL_SECTION;
                sendBuffer->timestamp = globalState.time;
                globalState.upperState.haveToken = 0;
                return 1;
            }
        } else {
            // process has not requested for critical section
            sendBuffer->type = REPLY_CRITICAL_SECTION;
            sendBuffer->timestamp = globalState.time;
            globalState.upperState.haveToken = 0;
            return 1;
        }
    } else if (buffer.type == REPLY_CRITICAL_SECTION) {
        globalState.upperState.messagesReceived++;
        globalState.upperState.repliesReceived++;
        if (globalState.upperState.repliesReceived == N_PROCESSES -1) {
            // can enter critical section
            enterCriticalSection();
            globalState.upperState.repliesReceived = 0;
            globalState.upperState.requestTimeStamp = -1;
            globalState.upperState.haveToken = 1;
            sendRepliesToDeferredList();
            globalState.phase++;
            // if phase is 40, computation is done
            // send computation end message to root node
            if (globalState.phase > 39) {
                if (globalState.number != ROOT_NODE+1) {
                    sendBuffer->type = COMPUTATION_END;
                    sendBuffer->timestamp = globalState.time;
                    globalState.upperState.messagesExchanged++;
                    globalState.upperState.totalMessagesSent++;
                    send(globalState.channels[0].outgoingSocket, (void *)sendBuffer, sizeof(message), 0);
                } else {
                    globalState.computation_end++;
                    processComputationEnd();
                }
                return 0;
            }

            *tv = (struct timeval *)malloc(sizeof(struct timeval));
            memset(*tv, 0, sizeof(struct timeval));
            if (globalState.phase >= 20 && 
                globalState.phase <= 40 &&
                globalState.number % 2 != 0) {
                (*tv)->tv_sec = rand()%PHASE_1_RAND + PHASE_2_DELAY;
                printf("(%d): Timer set to %d\n", globalState.time, (int)(*tv)->tv_sec);
                globalState.upperState.timerStart = globalState.time;
                globalState.upperState.timerValue = (int)(*tv)->tv_sec;
            } else {
                if (globalState.phase <= 40) {
                    (*tv)->tv_sec = rand()%PHASE_1_RAND + PHASE_1_DELAY;
                    printf("(%d): Timer set to %d\n", globalState.time, (int)(*tv)->tv_sec);
                    globalState.upperState.timerStart = globalState.time;
                    globalState.upperState.timerValue = (int)(*tv)->tv_sec;
                } else {
                    free(*tv);
                    *tv = NULL;
                }
            }
        }
        return 0;
    }
    return 0;
}

/* socket structure init function */
void initAddress(struct sockaddr_in *addr, short family, unsigned int port, int processId) {
    addr->sin_family = family;
    addr->sin_port = htons(port);
    inet_aton(serverIPs[processId-1], &(addr->sin_addr));
}

/* Function to init global process structure */
int initGlobalState(int processId) {
    // reset time
    globalState.time = 0;
    globalState.number = processId;
    globalState.diff = 1;
    globalState.state = PROCESS_UNINITED;

    globalState.upperState.requestTimeStamp = -1;
    globalState.upperState.haveToken = 0;
    globalState.upperState.repliesReceived = 0;
    globalState.upperState.messagesExchanged = 0;
    globalState.upperState.messagesSent = 0;
    globalState.upperState.messagesReceived = 0;
    globalState.upperState.totalMessagesSent = 0;
    globalState.upperState.totalMessagesReceived = 0;
    memset(&(globalState.upperState.timeOfRequest), 0, sizeof(struct timeval));
    globalState.upperState.timerStart = 0;
    globalState.upperState.timerValue = 0;

    memset(globalState.deferredList, -1, N_PROCESSES);
    globalState.deferredCount = 0;
    globalState.computation_end = 0;

    globalState.phase = 0;

    int i = 0;
    int j = 0;
    for (i = 0; i < N_PROCESSES; i++) {
        for (j = 0; j < N_PROCESSES; j++) {
            globalState.knownEvents[i][j] = 0;
        }
        globalState.channels[i].processId = i+1;
        globalState.channels[i].incomingSocket = -1;
        globalState.channels[i].outgoingSocket = -1;
        globalState.channels[i].incomingState = UNINITED;
        globalState.channels[i].outgoingState = UNINITED;
        memset(&(globalState.channels[i].addr), 0, sizeof(struct sockaddr_in));
    }
    globalState.server_link.serverSocket = -1;
    globalState.server_link.serverSocketState = UNINITED;
    memset(&(globalState.server_link.addr), 0, sizeof(struct sockaddr_in));

    // initialize server sockets
    initAddress(&(globalState.server_link.addr), AF_INET, SERVER_PORT, processId);
    globalState.server_link.serverSocketState = INITING;
    globalState.server_link.serverSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (globalState.server_link.serverSocket < 0) {
        globalState.server_link.serverSocketState = UNINITED;
        printf("(%d): Fatal error: socket initialization failed\n", globalState.time);
        return -1;
    }
    globalState.server_link.serverSocketState = INITED;
    if (bind(globalState.server_link.serverSocket, (struct sockaddr *)&(globalState.server_link.addr), sizeof(struct sockaddr))) {
        perror("Server Bind Failed");
        return -1;
    }
    return 0;
}

/* Function to connect to other processes listed above process(index) in serverIPs */
int connectAsClient(int index) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    initAddress(&addr, AF_INET, SERVER_PORT, index+1);

    struct sockaddr_in selfAddress;
    memcpy(&selfAddress, &(globalState.server_link.addr), sizeof(struct sockaddr_in));
    selfAddress.sin_port = htons(CLIENT_PORT);

    int clientSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket < 0) {
        printf("(%d): Error connecting to %d\n", globalState.time, index+1);
        return -1;
    }
    unsigned int client_port = CLIENT_PORT;
    while (bind(clientSocket, (struct sockaddr *)&selfAddress, sizeof(struct sockaddr_in)) && (client_port <= 65535)) {
        client_port ++;
        selfAddress.sin_port = htons(client_port);
        if (client_port == 65535) {
            close(clientSocket);
            return -1;
        }
    }

    int tries = MAX_TRIES;
    while (tries != 0) {
        if (!connect(clientSocket, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))) {
            globalState.channels[index].outgoingSocket = clientSocket;
            globalState.channels[index].processId = index+1;
            globalState.channels[index].outgoingState = CONNECTED;
            memcpy(&(globalState.channels[index].addr), &addr, sizeof(struct sockaddr_in));
            return 0;
        }
        sleep(1);
        tries--;
    }
    return -1;
}

/* Function to identify channel based on socket identifier and reset states */
void findAndResetSocketConnections(int socket) {
    int i = 0;
    for (i = 0; i < N_PROCESSES; i++) {
        if (globalState.channels[i].incomingSocket == socket) {
            globalState.channels[i].incomingSocket = -1;
            globalState.channels[i].incomingState = DISCONNECTED;
            close(globalState.channels[i].outgoingSocket);
            globalState.channels[i].outgoingState = DISCONNECTED;
            break;
        } else if (globalState.channels[i].outgoingSocket == socket) {
            globalState.channels[i].outgoingSocket = -1;
            globalState.channels[i].outgoingState = DISCONNECTED;
            close(globalState.channels[i].incomingSocket);
            globalState.channels[i].incomingState = DISCONNECTED;
            break;
        }
    }
}

/* Function to find the number of connected processes */
int findNumberConnectedProcesses() {
    int i = 0;
    int connected = 0;
    for (i = 0; i < N_PROCESSES; i++) {
        if (globalState.channels[i].incomingState == CONNECTED &&
            globalState.channels[i].outgoingState == CONNECTED) {
            connected++;
        }
    }
    return connected;
}

/* Function to start processing socket connections and incoming/outgoing messages and timer */
void start() {
    int index = 0;
    fd_set fds;
    fd_set reusefds;
    int maxSocket = -1;
    int fdsize = 0;
    FD_ZERO(&fds);
    FD_ZERO(&reusefds);
    for (index = 0; index < N_PROCESSES; index++) {
        if (globalState.channels[index].outgoingState == CONNECTED) {
            if (globalState.channels[index].outgoingSocket > maxSocket) {
                maxSocket = globalState.channels[index].outgoingSocket;
            }
        }
    }
    fdsize++;
    FD_SET((globalState.server_link.serverSocket), &reusefds);
    if (maxSocket < globalState.server_link.serverSocket)
        maxSocket = globalState.server_link.serverSocket;
    int done = 0;
    struct timeval *tv = NULL;
    while (!done) {
        FD_ZERO(&fds);
        if (fdsize == 0) {
            done = 1;
            if (tv != NULL)
                free(tv);
            continue;
        }
        memcpy(&fds, &reusefds, maxSocket+1);
        // if all channels are established, start timer to trigger mutual exclusion process
        // if all channels are not established, restart timer
        int selectResult = select(maxSocket+1, &fds, NULL, NULL, tv);
        if (selectResult < 0) {
            done = 1;
            break;
        } else if (tv != NULL) {
            if (tv->tv_sec <= 0) {
                if (!requestMutualExclusion()) {
                    enterCriticalSection();
                    globalState.phase++;
                    globalState.upperState.requestTimeStamp = -1;
                    if (globalState.phase >= 20 &&
                        globalState.phase <= 40 && 
                        globalState.number % 2 != 0) {
                        tv->tv_sec = rand()%PHASE_1_RAND + PHASE_2_DELAY;
                    } else {
                        if (globalState.phase <= 40)
                            tv->tv_sec = rand()%PHASE_1_RAND + PHASE_1_DELAY;
                        else {
                            free(tv);
                            tv = NULL;
                        }
                    }
                } else {
                    free(tv);
                    tv = NULL;
                }
            }
            if (selectResult == 0)
                continue;
        }

        int i = 0;
        int newMax = maxSocket;
        for (i = 0; i <= maxSocket; i++) {
            if (FD_ISSET(i, &fds)) {
                if (globalState.server_link.serverSocket == i) {
                    // server socket. Incoming connection ?
                    // accept connection
                    struct sockaddr_in clientAddr;
                    unsigned addrLen = sizeof(struct sockaddr);
                    memset(&clientAddr, 0, addrLen);
                    int acceptResult = accept(globalState.server_link.serverSocket, (struct sockaddr *)&clientAddr, &addrLen);
                    if (acceptResult < 0) {
                        printf("(%d): Connection accept failed\n", globalState.time);
                        continue;
                    }
                    // check ip of client and get process id 
                    // TODO remove dependency of ip and process id
                    char *ip = inet_ntoa(clientAddr.sin_addr);
                    int j = 0;
                    for (j = 0; j < N_PROCESSES; j++) {
                        if (!strcmp(serverIPs[j], ip)) {
                            break;    
                        }
                    }
                    // save process id, client socket in incomingsocket
                    globalState.channels[j].processId = j+1;
                    globalState.channels[j].incomingSocket = acceptResult;
                    globalState.channels[j].incomingState = CONNECTED;
                    // if outgoingsocket is not inited, connect to the server socket of the client
                    if (globalState.channels[j].outgoingState != CONNECTED) {
                         connectAsClient(j);
                    }
                    fdsize++;
                    FD_SET(globalState.channels[j].incomingSocket, &reusefds);
                    if (acceptResult > newMax) {
                        newMax = acceptResult;
                    }
                    // check channels and start timer if all channels are established
                    // timer in the range [5, 10] time units
                    if (findNumberConnectedProcesses() == N_PROCESSES - 1) {
                        // start timer in [5, 10] range
                        tv = (struct timeval *)malloc(sizeof(struct timeval));
                        memset(tv, 0, sizeof(struct timeval));
                        tv->tv_sec = rand()%PHASE_1_RAND + PHASE_1_DELAY;
                        printf("(%d): Timer set to %d\n", globalState.time, (int)tv->tv_sec);
                        globalState.upperState.timerStart = globalState.time;
                        globalState.upperState.timerValue = (int)tv->tv_sec;
                    } else {
                        tv = NULL;
                    }
                } else {
                    // client sockets. Incoming data ?
                    message buffer;
                    buffer.type = 0;
                    buffer.timestamp = 0;
                    message sendBuffer;
                    sendBuffer.type = 0;
                    int readBytes = recv(i, (void *)&buffer, sizeof(buffer), 0);
                    int processId = getProcessId(i);
                    if (readBytes <= 0) {
                        int k = 0;
                        for (k = 0; k < N_PROCESSES; k++) {
                            close(globalState.channels[k].incomingSocket);
                            close(globalState.channels[k].outgoingSocket);
                            globalState.channels[k].incomingState = DISCONNECTED;
                            globalState.channels[k].outgoingState = DISCONNECTED;
                            FD_CLR(globalState.channels[k].incomingSocket, &reusefds);
                            fdsize-=1;
                        }
                        close(globalState.server_link.serverSocket);
                    } else {
                        // message received, get the process id
                        // buffer until causal delivery is fulfilled
                        // deliver buffer when necessary
                        // if message type is request
                        //     if process has made a request:
                        //         if timestamp(selfRequest) < messagerequest or timestamps are equal and id of self is less
                        //             donot send reply
                        //    if process is not in critical section
                        //        send reply
                        //    if process is in critical section? can this happen ?
                        //        donot send reply
                        // for cases 'donot send reply, add the requests to queue'
                        // if message type is reply
                        //     decrement number of replies
                        //     if replies == 0 
                        //         enter critical section
                        //         process the file
                        //         exit critical section
                        //         send replies to the requests in the queue
                        //         restart the timer for mutex execution
                        if (upperLayer(buffer, &sendBuffer, processId, &tv) == 1) {
                            int sendSocket = getSendSocketByProcessId(processId);
                            if (sendSocket < 0) {
                                printf("(%d): Send Socket closed???", globalState.time);
                            } else {
                                globalState.upperState.messagesExchanged++;
                                globalState.upperState.totalMessagesSent++;
                                int sendBytes = send(sendSocket, (void *)&sendBuffer, sizeof(sendBuffer), 0);
                                if (sendBytes != sizeof(sendBuffer)) {
                                    printf("(%d): Send failed?\n", globalState.time);
                                }
                            }
                        }
                    }
                }
            }
        }
        maxSocket = newMax;
    }
}

/* Main function to run DCS */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("USAGE: dcs <id>\n");
        return -1;
    }

    int processId = atoi(argv[1])+1;
    if (processId < 1 || processId > N_PROCESSES) {
        printf("Please use ids from 1 to %u\n", N_PROCESSES);
        return -1;
    }
    if (initGlobalState(processId)) {
        printf("Fatal Global State init failure\n");
        return -1;
    }

    int index = 0;
    for (index = 0; index < processId-1; index++) {
        // client to IPs above processId-1 in serverIPs
        connectAsClient(index);
    }

    if (listen(globalState.server_link.serverSocket, BACKLOG)) {
        printf("(%d): Listen failure\n", globalState.time);
        // deinit sockets
        return -1;
    }

    start();
    printf("(%d): COMPUTATION END \n", globalState.time);
    printf("(%d): Total messages exchanged: %d\n", globalState.time, globalState.upperState.messagesExchanged);
    printf("(%d): Total messages sent: %d Total messages received: %d\n", globalState.time, globalState.upperState.totalMessagesSent, globalState.upperState.totalMessagesReceived);

    char buffer[100] = {0,};
    sprintf(buffer, "OUTPUT%d.txt", (globalState.number-1));
    FILE *outputFile = fopen(buffer, "a");
    if (outputFile != NULL) {
        memset(buffer, 0, 100);
        sprintf(buffer, "(%d): Total Messages Exchanged: %d\n", globalState.time, globalState.upperState.messagesExchanged);
        fwrite(buffer, 1, strlen(buffer), outputFile);
        memset(buffer, 0, 100);
        sprintf(buffer, "(%d): Total messages sent: %d Total messages received: %d\n", globalState.time, globalState.upperState.totalMessagesSent, globalState.upperState.totalMessagesReceived);
        fwrite(buffer, 1, strlen(buffer), outputFile);
        fclose(outputFile);
    }
    return 0;
}
