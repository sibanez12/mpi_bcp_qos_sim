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
void runServer(int serverThreadsPerHost, int serverProcessingTime,
		int coresForHPThreads, char *serverType) {

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
	threadState.coresForHPThreads = coresForHPThreads;
	threadState.serverType = serverType;
	threadState.threadID = threadID;
	threadState.serverID = serverID;
	threadState.data = malloc(sizeof(bigArray));
	threadState.data->size = BIG_ARRAY_SIZE;
	char filename[100];
	sprintf(filename, "./out/Server-%d__Thread-%d.log", serverID, threadID);
	threadState.logFile = initLog(filename, rankMap);
	threadState.logFile_isOpen = True;
	threadState.numREQmsgs = 0;
	threadState.seed = 1202107158 * my_rank + threadID * 1999; // seed RNG once for each thread

	/* TODO: Bind threads to cores */

	server_runThread(&threadState);

	server_cleanup(&threadState);
}

void server_runThread(serverThreadState *threadState) {
	int threadID = threadState->threadID;
	int serverID = threadState->serverID;
	int serverProcTime = threadState->serverProcTime;
	unsigned long int *seed = &(threadState->seed);
	bigArray *data = threadState->data;

	/* TODO: only used HP comm for now */
	MPI_Comm comm = highPriority_comm;

	MPI_Status status;
	mpiMsg msgBuf;
	server_writeLog = False;
	while(1) {
		/* receive REQUEST message from client node and send back ACK */
		server_receiveWrapper(&msgBuf, 1, MPI_ANY_SOURCE, MPI_ANY_TAG,
				comm, &status, threadState);
		DEBUG_PRINT(("SERVER %d - thread %d - received message: %s\n",
				serverID, threadID, msgBuf.message));

		if (strcmp(msgBuf.message, "REQUEST") == 0) {
			threadState->numREQmsgs++;
			perform_task(data, serverProcTime, seed);

			/* send ACK back */
			create_message(&msgBuf, "ACK", msgBuf.cont_index, msgBuf.threadID);
			server_sendWrapper(&msgBuf, 1, mpi_message_type, status.MPI_SOURCE, status.MPI_TAG, comm);
			DEBUG_PRINT(("SERVER %d - thread %d, sent ACK message\n", serverID, threadID));
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

void writeServerLog(serverThreadState *threadState) {
	if (threadState->logFile_isOpen == True) {
		int threadID = threadState->threadID;
		int serverID = threadState->serverID;
		fprintf(threadState->logFile,
				"###########################\n"
				"SERVER %d - THREAD ID: %d\n"
				"--------------------------\n"
				"Num REQUEST msgs = %llu\n"
				"###########################\n",
				serverID, threadID, threadState->numREQmsgs);
		fclose(threadState->logFile);
		threadState->logFile_isOpen = False;
		printf("SERVER %d -- THREAD %d ==> DONE reporting Stats\n", serverID, threadID);
	}
}
