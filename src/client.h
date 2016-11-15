/*
 * client.h
 *
 *  Created on: Oct 20, 2016
 *      Author: sibanez
 */

#ifndef CLIENT_H_
#define CLIENT_H_

/* Ignore at least the first few client requests to allow
 * for a warmup period
 */

#include <time.h>
#include "common.h"
#include "hdr_histogram.h"

/* global variable that is modified by signal handler to
 * dump statistics / end the simulation */
bool volatile client_keepRunning;

/* counts the total number of requests completed on all client
 * "sessions" */
unsigned long long totalNumReqsSeen;

///*
// * continuations to allow client threads to have more
// * than one outstanding thread at a time
// */
//typedef struct clientContinuation_s {
//	int targetServerID; // target server for this channel
//	time_t init_start_time; // marks the beginning of all the requests we are measuring
//	time_t start_time; // marks the beginning of the beginning of the requests for this group
//	bool start_time_isSet;
//	int numReqsCompleted; // counts from 0 --> WARMUP_THRESH at start up and 0 --> numClientReq while running
//	double avgCTsum; // the average of all of the completion times (accumulates as long as the user wants)
//	double numSamples; // the number of groups of numClientReq requests that have completed
//	double avgCTsqSum;
//	time_t final_time;
//	mpiMsg messageBuf;
//} clientContinuation;

typedef struct clientThreadState_s {
	int clientID;
	int threadID;
	int clientHPReqRate;
	int clientLPReqRate;
	int clientReqGrpSize;
	int numHosts;
	unsigned long int seed;
	FILE *clientLog;
	FILE *clientHistData;
	struct hdr_histogram* histogram;
	bool finishedLogging;
	struct timespec lastLPReqTime;
	struct timespec lastHPReqTime;
	long double avgHPReqCTsum; // counts seconds
	long double numHPReqSamples;
	struct timespec init_start_time;
	struct timespec final_time;
	int targetServerID;
	unsigned long long warmupCount;
} clientThreadState;

void runClient(int clientThreadsPerHost, int clientHPReqRate, int clientLPReqRate,
		int clientReqGrpSize, int numHosts);

void client_runThread(clientThreadState *threadState);

void client_checkSendLPReq(clientThreadState *threadState);

void client_checkSendRecvHPReq(clientThreadState *threadState);

void client_waitHPReply(clientThreadState *threadState);

void client_updateStats(clientThreadState *threadState);

bool client_shouldSend(int rate, struct timespec *lastPKtSendTime);

void client_sendReq(MPI_Comm comm, clientThreadState *threadState);

int chooseServerID(clientThreadState *threadState);

void chooseServerRank(int *targetServerRank, int targetServerID,
		clientThreadState *threadState, bool isHighPriority);

void client_partitionSessions(int *clientSessionsArray, int clientReqPerHost,
		int clientThreadsPerHost);

void client_intHandler(int sig_num);

void client_reportFinalStats(clientThreadState *threadState);

void client_logChosenServers(clientThreadState *threadState);

void client_ReceiveOne_spin(mpiMsg *msgBuf, int count, MPI_Datatype type, int source,
		MPI_Comm comm, MPI_Status *status, clientThreadState *threadState) ;

#endif /* CLIENT_H_ */
