/*
 * client.c
 *
 *  Created on: Oct 20, 2016
 *      Author: sibanez
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

#include "mpi.h"
#include "common.h"
#include "client.h"

/*  1. Client sends requests to server
 *  2. Waits to ACK back from server
 */
void runClient(int clientThreadsPerHost, int clientReqPerHost, int clientReqGrpSize,
		int numHosts) {

	signal(SIGUSR1, client_intHandler);

	int my_rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

	int clientID = rankMap[my_rank].clientID;

//	// Used to stall so we can attach to gdb
//	int i = 0;
//	char hostname[256];
//	gethostname(hostname, sizeof(hostname));
//	printf("***********************PID %d on %s ready for attach (CLIENT %d)\n", getpid(), hostname, clientID);
//	fflush(stdout);
//	while (0 == i)
//		sleep(5);

	/* Determine how many clientSesssions this thread should be responsible for */
	int clientSessionsArray[clientThreadsPerHost]; // array mapping {threadID ==> numClientSessions}
	client_partitionSessions(clientSessionsArray, clientReqPerHost, clientThreadsPerHost);
	int threadID = my_rank % clientThreadsPerHost;
	int numClientSessions = clientSessionsArray[threadID];

	totalNumReqsSeen = 0;

	clientThreadState threadState;
	threadState.clientID = clientID;
	threadState.threadID = threadID;
	threadState.clientReqPerHost = clientReqPerHost;
	threadState.clientReqGrpSize = clientReqGrpSize;
	threadState.numHosts = numHosts;
	threadState.numClientSessions = numClientSessions;
	// seed RNG once for each thread
	threadState.seed = 1202107158 * my_rank + threadID * 1999; // different seed for each thread
	/* Create log for this client process */
	char filename[100];
	sprintf(filename, "./out/Client-%d.log", clientID); // different log for each client process
	threadState.clientLog = initLog(filename, rankMap);
	threadState.finishedLogging = False;

	client_runThread(&threadState);

	/* Send out message to all processes indicating that the simulation has completed. */
	mpiMsg msgBuf;
	create_message(&msgBuf, "SIM COMPLETE", 0, 0);

	int tag = 0;
	int p;
	MPI_Comm_size(MPI_COMM_WORLD, &p);

	for (int dest = 0; dest < p; dest++) {
		if (rankMap[dest].isServer == True) {
			MPI_Send(&msgBuf, 1, mpi_message_type, dest, tag, highPriority_comm);
		}
	}
}


void client_runThread(clientThreadState *threadState) {

	int clientID = threadState->clientID;
	int numClientSessions = threadState->numClientSessions;

	/* Keep continuation state on the stack for this thread.
	 * Each continuation keeps track of an outstanding client request*/
	clientContinuation contState[numClientSessions];
	client_initContState(contState, threadState);

	/* TODO: Use only high priority communicator for now... */
	MPI_Comm comm = highPriority_comm;

	/* Immediately send out all of the requests for this thread */
	for (int cont_index=0; cont_index < numClientSessions; cont_index++) {
		client_sendReq(cont_index, comm, contState, threadState);
	}

	/* NOTE:
	 * rank ==> associates message to specific client thread
	 * cont_index ==> associates message with continuation object in the thread
	 */
	int numReqsOpen = numClientSessions;
	MPI_Status status;
	mpiMsg msgBuf;
	client_keepRunning = True;
	while (numReqsOpen > 0) {
		/* Always use in-thread spinning because we should never have more client threads than cores */
		client_ReceiveOne_spin(&msgBuf, 1, mpi_message_type, MPI_ANY_SOURCE,
				comm, &status, threadState, contState);
		DEBUG_PRINT(("CLIENT %d: received message %s\n", clientID, msgBuf.message));
		assert(msgBuf.cont_index < numClientSessions);
		int expectedServerID = contState[msgBuf.cont_index].targetServerID;
		int targetServerID = rankMap[status.MPI_SOURCE].serverID;
		assert(targetServerID == expectedServerID); // make sure message came from one of expected ranks
		if (strcmp(msgBuf.message, "ACK") == 0) {
			numReqsOpen--;
			client_updateContState_onACK(contState, msgBuf.cont_index, threadState);
			/* Create a new request and send it out with the same reqID */
			if (client_keepRunning == True) {
				client_sendReq(msgBuf.cont_index, comm, contState, threadState);
				numReqsOpen++;
			}
		}
		else {
			printf("ERROR: CLIENT %d -- received unknown message from process %d: %s\n",
					clientID, status.MPI_SOURCE, msgBuf.message);
		}

		if (client_keepRunning == False) {
			client_reportFinalStats(threadState, contState);
		}
	}

}

void client_sendReq(int cont_index, MPI_Comm comm, clientContinuation *contState,
		clientThreadState *threadState) {

	mpiMsg *msgBuf = &(contState[cont_index].messageBuf);
	MPI_Request req;
	int tag = 0;

	/* Each request can go to a different server */
	int targetServerID = contState[cont_index].targetServerID;
	int targetServerRank, targetServerThreadID;
	chooseServerRank(&targetServerRank, &targetServerThreadID, targetServerID, threadState);
	create_message(msgBuf, "REQUEST", cont_index, targetServerThreadID);

	/* send REQ message to the server
	 * Using non-blocking send*/
	MPI_Isend(msgBuf, 1, mpi_message_type, targetServerRank, tag, comm, &req);

	DEBUG_PRINT(("CLIENT %d -- thread %d -- sent REQUEST message\n", threadState->clientID, threadState->threadID));

}


/* Choose the server to send to:
 *    - pick random serverIDs until we find a server that is not on the same host
 *    - returns the serverID of the chosen server
 */
int chooseServerID(clientThreadState *threadState) {
	int numHosts = threadState->numHosts;
	int numServers = numHosts; // assuming one Server per host
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
 * TODO:
 */
void chooseServerRank(int *targetServerRank, int *targetServerThreadID,
		int targetServerID, clientThreadState *threadState) {
	entityEntry targetServer = serverMap[targetServerID];

	unsigned long int *seed = &(threadState->seed);

	int randVal = myRandom(seed);
	int index = randVal % targetServer.count;
	*targetServerRank = targetServer.ranks[index];
	*targetServerThreadID = 0;
}

/*
 * Partition the client sessions as equally as possible amongst the numClientThreads
 */
void client_partitionSessions(int *clientSessionsArray, int clientReqPerHost, int clientThreadsPerHost) {

	for (int threadID=0; threadID < clientThreadsPerHost; threadID++) {
		clientSessionsArray[threadID] = clientReqPerHost/clientThreadsPerHost;
	}

	int remainingSessions = clientReqPerHost - (clientReqPerHost/clientThreadsPerHost)*clientThreadsPerHost;

	for (int threadID=0; threadID < remainingSessions; threadID++) {
		clientSessionsArray[threadID] += 1;
	}
}

void client_initContState(clientContinuation *contState, clientThreadState *threadState) {
	int numClientSessions = threadState->numClientSessions;
    for (int i=0; i < numClientSessions; i++) {
    	contState[i].targetServerID = chooseServerID(threadState);
    	contState[i].start_time_isSet = False;
    	contState[i].numReqsCompleted = 0;
    	contState[i].avgCTsum = 0;
    	contState[i].numSamples = 0;
    	contState[i].avgCTsqSum = 0;
    }
    client_logChosenServers(contState, threadState);
}

/*
 * Updates the client channel continuation state and does logging if the simulation has completed.
 */
void client_updateContState_onACK(clientContinuation *contState, int cont_index,
		clientThreadState *threadState) {
	int clientReqGrpSize = threadState->clientReqGrpSize;

	clientContinuation *cont = &(contState[cont_index]);
	cont->numReqsCompleted++;
	if (totalNumReqsSeen >= WARMUP_THRESH && cont->start_time_isSet == False) {
		/* Ignore the first WARMUP_THRESH requests for each session */
		cont->start_time = time(NULL);
		cont->start_time_isSet = True;
		cont->init_start_time = time(NULL);
	}
	else if (cont->numReqsCompleted > clientReqGrpSize && cont->start_time_isSet == True) {
		/* This channel has completed numClientReqs requests  */
		time_t final_time = time(NULL);
		double totalCompletionTime = difftime(final_time, cont->start_time);
		double avgCompletionTime = totalCompletionTime/(double)clientReqGrpSize;
		cont->avgCTsum += avgCompletionTime;
		cont->avgCTsqSum += pow(avgCompletionTime, 2.0);
		cont->numSamples++;
		cont->numReqsCompleted = 0; // reset back to 0 so it can increment up to numClientReq
		cont->start_time = time(NULL); // reset
		cont->final_time = final_time;
	} else if (totalNumReqsSeen < WARMUP_THRESH) {
		totalNumReqsSeen++;
	}
}

/*
 * Function that is called when SIGUSR1 is sent to process
 */
void client_intHandler(int sig_num) {
	int my_rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
	int clientID = rankMap[my_rank].clientID;
	printf("************* CLIENT %d -- caught signal %d *****************\n", clientID, sig_num);
	client_keepRunning = False;
}

/*
 * Report the final statistics of the simulation
 */
void client_reportFinalStats(clientThreadState *threadState, clientContinuation *contState) {
	if (threadState->finishedLogging == False) {
		FILE *clientLog = threadState->clientLog;
		int threadID = threadState->threadID;
		int numClientSessions = threadState->numClientSessions;
		int clientReqGrpSize = threadState->clientReqGrpSize;
		int clientID = threadState->clientID;

		for (int cont_index = 0; cont_index < numClientSessions; cont_index++) {
			clientContinuation *cont = &(contState[cont_index]);

			/* This is the last response we will see for this channel so log the stats */
			double avgCT = cont->avgCTsum/cont->numSamples;
			double varCT = cont->avgCTsqSum/cont->numSamples - pow(avgCT, 2.0);
			double totalNumReqs = cont->numSamples*clientReqGrpSize;
			double totalTime = difftime(cont->final_time, cont->init_start_time);

			int my_rank;
			MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
			int clientID = rankMap[my_rank].clientID;
			fprintf(clientLog,
					"####################################\n"
					"CLIENT %d -- Channel %d\n"
					"Final Statistics\n"
					"-----------------------------\n"
					"Average Completion Time = %f\n"
					"Variance of Completion Times = %f\n"
					"Number of Samples = %f\n"
					"-----------------------------\n"
					"Total Time = %f\n"
					"Total Num Requests = %f\n"
					"Requests/sec = %f\n"
					"#####################################\n",
					clientID, cont_index, avgCT, varCT, cont->numSamples, totalTime,
					totalNumReqs, totalNumReqs/totalTime);
		}

		fclose(clientLog);
		printf("CLIENT %d -- THREAD %d ==> DONE reporting Stats\n", clientID, threadID);
		threadState->finishedLogging = True;
	}
}

void client_logChosenServers(clientContinuation *contState, clientThreadState *threadState) {
	int numClientSessions = threadState->numClientSessions;
	FILE *clientLog = threadState->clientLog;
	int clientID = threadState->clientID;

	int targetServerID;
	for (int i=0; i < numClientSessions; i++) {
		targetServerID = contState[i].targetServerID;
		fprintf(clientLog,
				"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
				"CLIENT %d -- Channel %d\n"
				"Target Server = %d\n"
				"Target Server Info:\n"
				"--------------------\n"
				"Host ID = %d\n"
				"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n",
				clientID, i, targetServerID,
				serverMap[targetServerID].hostID);
	}
}

/*
 * Wrapper around the nonblocking receive so that we can spin in
 * userspace rather than in the OS.
 */
void client_ReceiveOne_spin(mpiMsg *msgBuf, int count, MPI_Datatype type, int source,
		MPI_Comm comm, MPI_Status *status, clientThreadState *threadState,
		clientContinuation *contState) {

	int tag = MPI_ANY_TAG;
	MPI_Request req;
	MPI_Irecv(msgBuf, count, type, source, tag, comm, &req);
	/* Spin in user space waiting for the ACK to arrive */
	bool flag = False;
	while (flag == False) {
		MPI_Test(&req,&flag,status);
		if (client_keepRunning == False) {
			client_reportFinalStats(threadState, contState);
		}
	}
}


