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
#include <time.h>

#include "mpi.h"
#include "common.h"
#include "client.h"

/*  1. Client sends requests to server
 *  2. Waits to ACK back from server
 */
void runClient(int clientThreadsPerHost, int clientHPReqRate, int clientLPReqRate,
		int clientReqGrpSize, int numHosts) {

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

	int threadID = my_rank % clientThreadsPerHost;

	totalNumReqsSeen = 0;

	clientThreadState threadState;
	threadState.clientID = clientID;
	threadState.threadID = threadID;
	threadState.clientHPReqRate = clientHPReqRate;
	threadState.clientLPReqRate = clientLPReqRate;
	threadState.clientReqGrpSize = clientReqGrpSize;
	threadState.numHosts = numHosts;
	threadState.seed = 1202107158 * my_rank + threadID * 1999; // different seed for each thread
	/* Create log for this client process */
	char filename[100];
	sprintf(filename, "./out/Client-%d.log", clientID); // different log for each client process
	threadState.clientLog = initLog(filename, rankMap);
	threadState.finishedLogging = False;
	struct timespec curr_time;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &(curr_time));
	// set the initial time to compare against for low priority messages
	threadState.lastLPReqTime = curr_time;
	// set the initial time to compare against for high priority messages
	threadState.lastHPReqTime = curr_time;
	threadState.avgHPReqCTsum = 0;
	threadState.numHPReqSamples = 0;
	threadState.targetServerID = chooseServerID(&threadState);
	threadState.init_start_time = curr_time;
	threadState.final_time.tv_sec = 0;
	threadState.final_time.tv_nsec = 0;
	threadState.warmupCount = 0;

	// run the client thread
	client_runThread(&threadState);

	/* Send out message to all processes indicating that the simulation has completed. */
	mpiMsg msgBuf;
	create_message(&msgBuf, "SIM COMPLETE", 0);

	int tag = 0;
	int p;
	MPI_Comm_size(MPI_COMM_WORLD, &p);

	for (int dest = 0; dest < p; dest++) {
		if (rankMap[dest].isServer == True) {
			MPI_Send(&msgBuf, 1, mpi_message_type, dest, tag, highPriority_comm);
		}
	}
}

/*
 * Continuously alternate between checking if it is time to send out a
 * low priority message and a high priority message
 */
void client_runThread(clientThreadState *threadState) {

	client_keepRunning = True;
	while(client_keepRunning == True) {
		client_checkSendLPReq(threadState);
		client_checkSendRecvHPReq(threadState);
	}
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &(threadState->final_time));
	client_reportFinalStats(threadState);
}

void client_checkSendLPReq(clientThreadState *threadState) {
	int clientLPReqRate = threadState->clientLPReqRate;
	struct timespec *lastLPReqTime = &(threadState->lastLPReqTime);

	bool shouldSendReq = client_shouldSend(clientLPReqRate, lastLPReqTime);

	if (shouldSendReq) {
		/* Send out a low priority request for this thread */
		MPI_Comm comm = lowPriority_comm;
		client_sendReq(comm, threadState);
	}
}

void client_checkSendRecvHPReq(clientThreadState *threadState) {
	int clientHPReqRate = threadState->clientHPReqRate;
	struct timespec *lastHPReqTime = &(threadState->lastHPReqTime);

	bool shouldSendReq = client_shouldSend(clientHPReqRate, lastHPReqTime);

	if (shouldSendReq) {
		/* Send out a high priority request for this thread */
		MPI_Comm comm = highPriority_comm;
		client_sendReq(comm, threadState);
		// reset the lastHPReqTime
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, lastHPReqTime);
		client_waitHPReply(threadState);
	}
}

/*
 * Wait for the high priority message reply to come back and
 * update statistics if it is past the warmup period
 */
void client_waitHPReply(clientThreadState *threadState) {
	int clientID = threadState->clientID;

	MPI_Status status;
	mpiMsg msgBuf;
	MPI_Comm comm = highPriority_comm;
	client_ReceiveOne_spin(&msgBuf, 1, mpi_message_type, MPI_ANY_SOURCE,
					comm, &status, threadState);
	DEBUG_PRINT(("CLIENT %d: received message: %s\n", clientID, msgBuf.message));
	int expectedServerID = threadState->targetServerID;
	int targetServerID = rankMap[status.MPI_SOURCE].serverID;
	assert(targetServerID == expectedServerID); // make sure message came from the expected rank
	if (strcmp(msgBuf.message, "HIGH PRIORITY ACK") == 0) {
		threadState->warmupCount++;
		bool past_warmup = (threadState->warmupCount > NUM_WARMUP_SAMPLES);
		if (past_warmup) {
			client_updateStats(threadState);
		}
	} else {
		printf("ERROR: CLIENT %d received unknown message %s\n", clientID, msgBuf.message);
	}
}

/*
 * TODO: Update the stats on arrival of the HP message response
 */
void client_updateStats(clientThreadState *threadState) {
	struct timespec curr_time;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &curr_time);

	struct timespec latency;
	int r = timespec_subtract(&latency, &curr_time, &(threadState->lastHPReqTime));
	if (r) printf("ERROR: current_time - last_time\n");

	threadState->numHPReqSamples += 1;

	long double latency_d = timespec_to_double(&latency);
	threadState->avgHPReqCTsum += latency_d;

}

/*
 * if: lastTime - (curr_time - ipg) < 0
 *     send new packet
 */
bool client_shouldSend(int rate, struct timespec *lastPKtSendTime) {

	struct timespec curr_time;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &curr_time);

	// compute the inter-packet gap based on the clientLPReqRate
	struct timespec ipg;
	compute_ipg(rate, &ipg);

	struct timespec curr_minus_ipg;
	int r = timespec_subtract (&curr_minus_ipg, &curr_time, &ipg);
	if (r == 1) printf("ERROR: current_time - ipg < 0\n");

	struct timespec temp;
	bool shouldSendReq = timespec_subtract(&temp, lastPKtSendTime, &curr_minus_ipg);
	return shouldSendReq;
}

void client_sendReq(MPI_Comm comm, clientThreadState *threadState) {
	int targetServerID = threadState->targetServerID;

	mpiMsg msgBuf;
	int tag = 0;

	bool isHighPriority = (comm == highPriority_comm);

	/* Each request can go to a different server */
	int targetServerRank;
	chooseServerRank(&targetServerRank, targetServerID, threadState, isHighPriority);
	if (isHighPriority)
		create_message(&msgBuf, "HIGH PRIORITY REQUEST", 0);
	else
		create_message(&msgBuf, "LOW PRIORITY REQUEST", 0);

	/* send REQ message to the server
	 * Using non-blocking send*/
	MPI_Send(&msgBuf, 1, mpi_message_type, targetServerRank, tag, comm);

	DEBUG_PRINT(("CLIENT %d -- thread %d -- sent %s message\n", threadState->clientID, threadState->threadID, msgBuf.message));

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
 * TODO: choose a random rank within the correct priority group
 */
void chooseServerRank(int *targetServerRank, int targetServerID,
		clientThreadState *threadState, bool isHighPriority) {
	serverEntry targetServer = serverMap[targetServerID];

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

/*
 * Partition the client sessions as equally as possible amongst the numClientThreads
 */
void client_partitionSessions(int *clientSessionsArray, int clientReqPerHost,
		int clientThreadsPerHost) {

	for (int threadID=0; threadID < clientThreadsPerHost; threadID++) {
		clientSessionsArray[threadID] = clientReqPerHost/clientThreadsPerHost;
	}

	int remainingSessions = clientReqPerHost - (clientReqPerHost/clientThreadsPerHost)*clientThreadsPerHost;

	for (int threadID=0; threadID < remainingSessions; threadID++) {
		clientSessionsArray[threadID] += 1;
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
void client_reportFinalStats(clientThreadState *threadState) {
	if (threadState->finishedLogging == False) {
		FILE *clientLog = threadState->clientLog;
		int threadID = threadState->threadID;
		int clientReqGrpSize = threadState->clientReqGrpSize;
		int clientID = threadState->clientID;

		/* This is the last response we will see for this channel so log the stats */
		long double avgHPReqCT = threadState->avgHPReqCTsum/threadState->numHPReqSamples;
		long double totalNumHPReqs = threadState->numHPReqSamples*clientReqGrpSize;
		struct timespec diff;
		int r = timespec_subtract(&diff, &(threadState->final_time), &(threadState->init_start_time));
		if (r) printf("ERROR: final_time - start time < 0\n");
		long double totalTime = timespec_to_double(&diff);

		int my_rank;
		MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
		fprintf(clientLog,
				"####################################\n"
				"CLIENT %d\n"
				"Final Statistics\n"
				"-----------------------------\n"
				"Average High Priority Request Completion Time = %Lf\n"
				"Number of High Priority Request Samples = %Lf\n"
				"-----------------------------\n"
				"Total Time = %Lf\n"
				"Total Num Requests = %Lf\n"
				"Requests/sec = %Lf\n"
				"#####################################\n",
				clientID, avgHPReqCT, threadState->numHPReqSamples, totalTime,
				totalNumHPReqs, totalNumHPReqs/totalTime);

		fclose(clientLog);
		printf("CLIENT %d -- THREAD %d ==> DONE reporting Stats\n", clientID, threadID);
		threadState->finishedLogging = True;
	}
}

void client_logChosenServers(clientThreadState *threadState) {
	FILE *clientLog = threadState->clientLog;
	int clientID = threadState->clientID;

	int targetServerID;
	targetServerID = threadState->targetServerID;
	fprintf(clientLog,
			"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
			"CLIENT %d\n"
			"Target Server = %d\n"
			"Target Server Info:\n"
			"--------------------\n"
			"Host ID = %d\n"
			"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n",
			clientID, targetServerID,
			serverMap[targetServerID].hostID);
}

/*
 * Wrapper around the nonblocking receive so that we can spin in
 * userspace rather than in the OS.
 */
void client_ReceiveOne_spin(mpiMsg *msgBuf, int count, MPI_Datatype type, int source,
		MPI_Comm comm, MPI_Status *status, clientThreadState *threadState) {

	int tag = MPI_ANY_TAG;
	MPI_Request req;
	MPI_Irecv(msgBuf, count, type, source, tag, comm, &req);
	/* Spin in user space waiting for the ACK to arrive */
	bool flag = False;
	while (flag == False) {
		MPI_Test(&req,&flag,status);
	}
}


