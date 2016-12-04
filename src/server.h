/*
 * server.h
 *
 *  Created on: Oct 20, 2016
 *      Author: sibanez
 */

#ifndef SERVER_H_
#define SERVER_H_

#include <time.h>
#include "hdr_histogram.h"

bool volatile server_writeLog;

typedef struct serverContinuation_s {
	int replyCount;
	int numRepliesNeeded;
	int originClientRank;
	struct timespec start_time;
} serverContinuation;

typedef struct serverThreadState_s {
	int serverThreadsPerHost;
	int serverMemLoad;
	int serverNetLoad;
	int serverComputeLoad;
	int coresForHPThreads;
	int numHosts;
	int threadID;
	int serverID;
	bigArray *data;
	FILE *serverHistData;
	FILE *logFile;
	bool logFile_isOpen;
	unsigned long long numHPReqMsgs;
	unsigned long long numLPReqMsgs;
	unsigned long int seed;
	bool isHighPriority;
	serverContinuation *continuations;
	int maxContinuations;
	bitVector *continuationVector;
	struct hdr_histogram* histogram;
} serverThreadState;

void runServer(int serverThreadsPerHost, int clientThreadsPerHost,
		int serverMemLoad, int serverNetLoad, int serverComputeLoad, int coresForHPThreads, int numHosts);

bool server_getPriority(serverThreadState *threadState);

void server_runThread(serverThreadState *threadState);

void server_sendWrapper(mpiMsg *msgBuf, int count, MPI_Datatype mpi_message_type,
		int source, int tag, MPI_Comm comm);

void server_cleanup(serverThreadState *threadState);

void server_intHandler(int sig_num);

int serverChooseServerID(serverThreadState *threadState);

void serverChooseServerRank(int *targetServerRank, int targetServerID,
		serverThreadState *threadState, bool isHighPriority);

void serverCreateNetworkRequest(serverThreadState *threadState, MPI_Comm comm, int tag);

void server_receiveWrapper(mpiMsg *msgBuf, int count, int source,
		int tag, MPI_Comm comm, MPI_Status *status, serverThreadState *threadState);

void writeServerLog(serverThreadState *threadState);

#endif /* SERVER_H_ */
