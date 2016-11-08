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
#define WARMUP_THRESH 100

#include "common.h"

/* global variable that is modified by signal handler to
 * dump statistics / end the simulation */
bool volatile client_keepRunning;

/* counts the total number of requests completed on all client
 * "sessions" */
unsigned long long totalNumReqsSeen;

/*
 * continuations to allow client threads to have more
 * than one outstanding thread at a time
 */
typedef struct clientContinuation_s {
	int targetServerID; // target server for this channel
	time_t init_start_time; // marks the beginning of all the requests we are measuring
	time_t start_time; // marks the beginning of the beginning of the requests for this group
	bool start_time_isSet;
	int numReqsCompleted; // counts from 0 --> WARMUP_THRESH at start up and 0 --> numClientReq while running
	double avgCTsum; // the average of all of the completion times (accumulates as long as the user wants)
	double numSamples; // the number of groups of numClientReq requests that have completed
	double avgCTsqSum;
	time_t final_time;
	mpiMsg messageBuf;
} clientContinuation;

typedef struct clientThreadState_s {
	int clientID;
	int threadID;
	int clientReqPerHost;
	int clientReqGrpSize;
	int numHosts;
	int numClientSessions;
	unsigned long int seed;
	FILE *clientLog;
	bool finishedLogging;
} clientThreadState;

void runClient( int clientThreadsPerHost, int clientReqPerHost, int clientReqGrpSize,
		int numHosts);

void client_runThread(clientThreadState *threadState);

void client_sendReq(int cont_index, MPI_Comm comm,
		clientContinuation *contState, clientThreadState *threadState);

int chooseServerID(clientThreadState *threadState);

void chooseServerRank(int *targetServerRank, int *targetServerThreadID,
		int targetServerID, clientThreadState *threadState);

void client_partitionSessions(int *clientSessionsArray, int clientSessionsPerHost,
		int clientThreadsPerHost);

void client_initContState(clientContinuation *contState, clientThreadState *threadState);

void client_updateContState_onACK(clientContinuation *contState, int cont_index,
		clientThreadState *threadState);

void client_intHandler(int sig_num);

void client_reportFinalStats(clientThreadState *threadState, clientContinuation *contState);

void client_logChosenServers(clientContinuation *contState, clientThreadState *threadState);

void client_ReceiveOne_spin(mpiMsg *messageBuf, int count, MPI_Datatype type, int source,
		MPI_Comm comm, MPI_Status *status, clientThreadState *threadState,
		clientContinuation *contState);

#endif /* CLIENT_H_ */
