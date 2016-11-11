/*
 * server.c
 *
 *  Created on: Oct 20, 2016
 *      Author: sibanez
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include "mpi.h"
#include "common.h"
#include "server.h"

/* 1. Server receives message from client
 * 2. Do required processing
 * 3. send ACK back to server
 */
void runServer(int serverThreadsPerHost, int serverProcessingTime, int serverNetLoad,
		int coresForHPThreads, int numHosts) {

	signal(SIGUSR1, server_intHandler);

	int my_rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
	int threadID = my_rank % serverThreadsPerHost;
	int serverID = rankMap[my_rank].serverID;

//	// Used to stall so we can attach to gdb
//	int i = 0;
//	char hostname[256];
//	gethostname(hostname, sizeof(hostname));
//	printf("***********************PID %d on %s ready for attach (SERVER %d)\n", getpid(), hostname, my_rank);
//	fflush(stdout);
//	while (0 == i)
//		sleep(5);

	/* Initialize the serverThreadState */
	serverThreadState threadState;
	threadState.serverThreadsPerHost = serverThreadsPerHost;
	threadState.serverProcTime = serverProcessingTime;
	threadState.serverNetLoad = serverNetLoad;
	threadState.coresForHPThreads = coresForHPThreads;
	threadState.numHosts = numHosts;
	threadState.threadID = threadID;
	threadState.serverID = serverID;
	threadState.data = malloc(sizeof(bigArray));
	threadState.data->size = BIG_ARRAY_SIZE;
	char filename[100];
	sprintf(filename, "./out/Server-%d__Thread-%d.log", serverID, threadID);
	threadState.logFile = initLog(filename, rankMap);
	threadState.logFile_isOpen = True;
	threadState.numHPReqMsgs = 0;
	threadState.numLPReqMsgs = 0;
	threadState.seed = 1202107158 * my_rank + threadID * 1999; // seed RNG once for each thread
	threadState.isHighPriority = server_getPriority(&threadState);

	/* TODO: Bind threads to cores */

	server_runThread(&threadState);

	server_cleanup(&threadState);
}

/*
 * Returns true if this is a high priority server thread
 */
bool server_getPriority(serverThreadState *threadState) {
	int threadID = threadState->threadID;
	int coresForHPThreads = threadState->coresForHPThreads;
	return (threadID < coresForHPThreads);

}

void server_runThread(serverThreadState *threadState) {
	int threadID = threadState->threadID;
	int serverID = threadState->serverID;
	int serverProcTime = threadState->serverProcTime;
	unsigned long int *seed = &(threadState->seed);
	bigArray *data = threadState->data;
	bool isHighPriority = threadState->isHighPriority;

	/* TODO: only used HP comm for now */
	MPI_Comm comm = (isHighPriority) ? highPriority_comm : lowPriority_comm;

	MPI_Status status;
	mpiMsg msgBuf;
	server_writeLog = False;
	while(1) {
		/* receive REQUEST message from client node and send back ACK */
		server_receiveWrapper(&msgBuf, 1, MPI_ANY_SOURCE, MPI_ANY_TAG,
				comm, &status, threadState);
		DEBUG_PRINT(("SERVER %d - thread %d - received message: %s\n",
				serverID, threadID, msgBuf.message));

		if (strcmp(msgBuf.message, "HIGH PRIORITY REQUEST") == 0) {
			threadState->numHPReqMsgs++;
			perform_task(data, serverProcTime, seed);

			/* send ACK back */
			create_message(&msgBuf, "HIGH PRIORITY ACK", msgBuf.threadID);
			server_sendWrapper(&msgBuf, 1, mpi_message_type, status.MPI_SOURCE, status.MPI_TAG, comm);
			DEBUG_PRINT(("SERVER %d - thread %d, sent %s message\n", serverID, threadID, msgBuf.message));
		} else if (strcmp(msgBuf.message, "LOW PRIORITY REQUEST") == 0) {
			threadState->numLPReqMsgs++;
			perform_task(data, serverProcTime, seed);
			/* Don't send back reply for low priority requests */
		}
		else if (strcmp(msgBuf.message, "SIM COMPLETE") == 0) {
			if (server_writeLog == True) {
				writeServerLog(threadState);
			}
		} else {
			printf("ERROR: SERVER %d - thread %d - received unknown message from process %d: %s\n",
					serverID, threadID, status.MPI_SOURCE, msgBuf.message);
		}
	}
}


void server_sendWrapper(mpiMsg *msgBuf, int count, MPI_Datatype mpi_logRecord_type,
		int source, int tag, MPI_Comm comm) {

	MPI_Send(msgBuf, 1, mpi_logRecord_type, source, tag, comm);
}

void server_cleanup(serverThreadState *threadState) {
	bigArray *data = threadState->data;
	free(data);
}


void server_intHandler(int sig_num) {
	int my_rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
	int serverID = rankMap[my_rank].serverID;
	printf("************* SERVER %d caught signal %d *****************\n", serverID, sig_num);
	server_writeLog = True;
}

/*
 * Server receive using spinning/pinning receive
 */
void server_receiveWrapper(mpiMsg *msgBuf, int count, int source,
		int tag, MPI_Comm comm, MPI_Status *status, serverThreadState *threadState) {

	MPI_Request req;
	MPI_Irecv(msgBuf, count, mpi_message_type, source, tag, comm, &req);
	/* Spin in user space waiting for the message to arrive */
	bool flag = False;
	while (flag == False) {
		MPI_Test(&req, &flag, status);
		if (server_writeLog == True) {
			writeServerLog(threadState);
		}
	}

}

/*
 *  Create, send and receive the response of a request to a different server.
 */
void serverCreateNetworkRequest(serverThreadState *threadState, MPI_Comm comm, int tag) {
	mpiMsg s2sMsgBuf;
	int targetServerRank;
	char *msg = "HIGH PRIORITY SERVER REQUEST";


	bool isHighPriority = (comm == highPriority_comm);
	if (!isHighPriority) {
		msg = "LOW PRIORITY SERVER REQUEST";
	}

	for (int i = 0; i < threadState->serverNetLoad; i++) {
		/* Each request can go to a different server */
		int targetServerID = serverChooseServerID(threadState);
		serverChooseServerRank(&targetServerRank, targetServerID, threadState, isHighPriority);

		/* Send the message but do not wait for ACK */
		create_message(&s2sMsgBuf, msg, 0);
		server_sendWrapper(&s2sMsgBuf, 1, mpi_message_type, targetServerRank, tag, comm);
		DEBUG_PRINT(("SERVER %d - thread %d - SENT REQUEST TO SERVER %d RANK %d\n",
						threadState->serverID, threadState->threadID, targetServerID, targetServerRank));
	}
}

/* Choose the server to send to:
 *    - pick random serverIDs until we find a server that is not on the same host as the current server
 *    - return the serverID of the chosen server
 */
int serverChooseServerID(serverThreadState *threadState) {
	int numServers = threadState->numHosts;
	unsigned long int *seed = &(threadState->seed);

	int my_rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
	int my_host = rankMap[my_rank].hostID;
	long int randVal = myRandom(seed);
	int serverID = randVal % numServers;

	/* Make sure the chosen server is on a different host */
	while (serverMap[serverID].hostID == my_host) {
		randVal = myRandom(seed);
		serverID = randVal % numServers;
	}

	return serverID;
}

/*
 * Choose the rank of the server to send to
 */
void serverChooseServerRank(int *targetServerRank, int targetServerID,
		serverThreadState *threadState, bool isHighPriority) {
	serverEntry targetServer = serverMap[targetServerID];

	DEBUG_PRINT(("ChooseServerRank from server %d\n", targetServerID));

	unsigned long int *seed = &(threadState->seed);
	int randVal = myRandom(seed);

	if (isHighPriority) {
		int index = randVal % targetServer.HPcount;
		*targetServerRank = targetServer.HPranks[index];
	} else {
		int index = randVal % targetServer.LPcount;
		*targetServerRank = targetServer.LPranks[index];
	}
}

void writeServerLog(serverThreadState *threadState) {
	if (threadState->logFile_isOpen == True) {
		int threadID = threadState->threadID;
		int serverID = threadState->serverID;
		fprintf(threadState->logFile,
				"###########################\n"
				"SERVER %d - THREAD ID: %d\n"
				"--------------------------\n"
				"Num High Priority REQUEST msgs = %llu\n"
				"Num Low Priority REQUEST msgs = %llu\n"
				"###########################\n",
				serverID, threadID, threadState->numHPReqMsgs, threadState->numLPReqMsgs);
		fclose(threadState->logFile);
		threadState->logFile_isOpen = False;
		printf("SERVER %d -- THREAD %d ==> DONE reporting Stats\n", serverID, threadID);
	}
}
