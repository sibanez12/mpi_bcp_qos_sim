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
void runServer(int serverThreadsPerHost, int clientThreadsPerHost, int serverMemLoad, int serverNetLoad,
		int serverComputeLoad, int coresForHPThreads, int numHosts) {

	signal(SIGUSR1, server_intHandler);

	int my_rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
	int threadID = my_rank % (serverThreadsPerHost + clientThreadsPerHost) - clientThreadsPerHost;
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
	threadState.serverMemLoad = serverMemLoad;
	threadState.serverNetLoad = serverNetLoad;
	threadState.serverComputeLoad = serverComputeLoad;
	threadState.coresForHPThreads = coresForHPThreads;
	threadState.numHosts = numHosts;
	threadState.threadID = threadID;
	threadState.serverID = serverID;
	threadState.data = malloc(sizeof(bigArray));
	threadState.data->size = BIG_ARRAY_SIZE;
	char filename[100];
	sprintf(filename, "./out/Server-%d__Thread-%d.log", serverID, threadID);
	threadState.logFile = initLog(filename, rankMap);
	threadState.logFile_isOpen = true;
	threadState.numHPReqMsgs = 0;
	threadState.numLPReqMsgs = 0;
	threadState.seed = 1202107158 * my_rank + threadID * 1999; // seed RNG once for each thread
	threadState.isHighPriority = server_getPriority(&threadState);
	/* FIXME Calculate dynamically the continuation size */
	threadState.continuations = malloc(5000 * sizeof(serverContinuation));
	threadState.maxContinuations = 5000;
	threadState.continuationVector = createBitVector(5000);
	char histfile[100];
        sprintf(histfile, "./out/Server-%d.hist", serverID); // different Hist Data for each server process
        threadState.serverHistData = initHistData(histfile, rankMap);

        hdr_init(
                        1,                                                                              // Minimum value = 1 ns
                        INT64_C(10000000000),   // Maximum value = 10 s
                        2,                                                                              // Number of significant figures after decimal
                        &(threadState.histogram) // Pointer to initialise
        );
        DEBUG_PRINT(("Initializing Histogram for client %d (%p) at %p\n", serverID, (void*)&threadState, (void*) &(threadState.histogram)));

	/* TODO: Bind threads to cores */
	DEBUG_PRINT(("SERVER %d - thread %d - rank %d - isHighPriority %d --> Initializing\n",
						serverID, threadID, my_rank, threadState.isHighPriority));

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
	int serverMemLoad = threadState->serverMemLoad;
	int serverComputeLoad = threadState->serverComputeLoad;
	unsigned long int *seed = &(threadState->seed);
	bigArray *data = threadState->data;
	bool isHighPriority = threadState->isHighPriority;

	MPI_Comm comm = (isHighPriority) ? highPriority_comm : lowPriority_comm;

	MPI_Status status;
	mpiMsg msgBuf;
	server_writeLog = true;
	while(1) {
		server_receiveWrapper(&msgBuf, 1, MPI_ANY_SOURCE, MPI_ANY_TAG,
				comm, &status, threadState);
		DEBUG_PRINT(("SERVER %d - thread %d - received message: %s\n",
				serverID, threadID, msgBuf.message));

		/* Receive HP request from a client */
		if (strcmp(msgBuf.message, "HIGH PRIORITY REQUEST") == 0) {
			threadState->numHPReqMsgs++;

			/* Do some local processing */
			perform_memory_task(data, serverMemLoad, seed);
			perform_compute_task(serverComputeLoad);

			/* Check whether server-2-server request is required */
			if (threadState->serverNetLoad == 0) {
				/* If not, just send an ACK */
				create_message(&msgBuf, "HIGH PRIORITY ACK", 0);
				server_sendWrapper(&msgBuf, 1, mpi_message_type, status.MPI_SOURCE, 0, comm);
				DEBUG_PRINT(("SERVER %d - thread %d, sent %s message\n", serverID, threadID, msgBuf.message));
			} else {
				/* Create server-2-server network load */
				int tag = getFirstOne(threadState->continuationVector);
				/* FIXME: Handle the case where the bitVector is full
                                 * This will only occur if the clients don't block in
                                 * every HP request any more.
                                 */
				serverCreateNetworkRequest(threadState, comm, tag);
				clearBit(threadState->continuationVector, tag);
				/* Initialize the continuation for this request */
			        clock_gettime(CLOCK_PROCESS_CPUTIME_ID,
						&threadState->continuations[tag].start_time);
				threadState->continuations[tag].replyCount = 0;
				threadState->continuations[tag].numRepliesNeeded = threadState->serverNetLoad;
				threadState->continuations[tag].originClientRank = status.MPI_SOURCE;
			}
			/* Can't send ACK back yet*/
		/* Receive LP request from a client */
		} else if (strcmp(msgBuf.message, "LOW PRIORITY REQUEST") == 0) {
			threadState->numLPReqMsgs++;

			/* Do some local processing */
			perform_memory_task(data, serverMemLoad, seed);
			perform_compute_task(serverComputeLoad);

			/* Create server-2-server network load if required */
			if (threadState->serverNetLoad != 0) {
				serverCreateNetworkRequest(threadState, comm, 0);
			}

		/* Receive HP request from a server */
		} else if (strcmp(msgBuf.message, "HIGH PRIORITY SERVER REQUEST") == 0) {
			/* FIXME: Do we need to update statistics here? */

			/* Do only local processing */
			perform_memory_task(data, serverMemLoad, seed);
			perform_compute_task(serverComputeLoad);

			/* Send ACK back */
			create_message(&msgBuf, "HIGH PRIORITY ACK", msgBuf.threadID);
			server_sendWrapper(&msgBuf, 1, mpi_message_type, status.MPI_SOURCE, status.MPI_TAG, comm);
		/* Receive LP request from a server */
		} else if (strcmp(msgBuf.message, "LOW PRIORITY SERVER REQUEST") == 0) {
			/* FIXME: Do we need to update statistics here? */

			/* Do only local processing */
			perform_memory_task(data, serverMemLoad, seed);
			perform_compute_task(serverComputeLoad);

			/* Send ACK back */
			create_message(&msgBuf, "LOW PRIORITY ACK", msgBuf.threadID);
			server_sendWrapper(&msgBuf, 1, mpi_message_type, status.MPI_SOURCE, status.MPI_TAG, comm);
		/* Receive HP ACK from a server */
		} else if (strcmp(msgBuf.message, "HIGH PRIORITY ACK") == 0){
			/* Identify the continuation to which this ACK belongs */
			serverContinuation *current_continuation = &threadState->continuations[status.MPI_TAG];

			/* Calculate the latency for the s2s message */
			struct timespec curr_time, latency;
		        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &curr_time);

			int r = timespec_subtract(&latency, &curr_time, &(current_continuation->start_time));
			if (r) printf("ERROR: current_time - last_time\n");

			long double latency_d = timespec_to_double(&latency);
			hdr_record_value(
				threadState->histogram,
				(int64_t)(latency_d*NSEC_PER_SEC)
			);

			/* Check whether this is the final message needed for this continuation */
			current_continuation->replyCount++;
			if (current_continuation->replyCount == current_continuation->numRepliesNeeded) {
				/* Send ACK message to the client */
				create_message(&msgBuf, "HIGH PRIORITY ACK", 0);
				server_sendWrapper(&msgBuf, 1, mpi_message_type, current_continuation->originClientRank,
							0, comm);
				DEBUG_PRINT(("SERVER %d - thread %d, sent %s message\n", serverID, threadID, msgBuf.message));

				/* Set the bitVector entry of this continuation to be free */
				setBit(threadState->continuationVector, status.MPI_TAG);
			}
		/* Receive LP ACK from a server */
		} else if (strcmp(msgBuf.message, "LOW PRIORITY ACK") == 0){
					/* Identify the continuation to which this ACK belongs */
					serverContinuation *current_continuation = &threadState->continuations[status.MPI_TAG];

					/* Check whether this is the final message needed for this continuation */
					current_continuation->replyCount++;
					if (current_continuation->replyCount == current_continuation->numRepliesNeeded) {
						/* Don't send back reply for LP requests */

						/* Set the bitVector entry of this continuation to be free */
						setBit(threadState->continuationVector, status.MPI_TAG);
					}
		} else if (strcmp(msgBuf.message, "SIM COMPLETE") == 0) {
			if (server_writeLog == true) {
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
	server_writeLog = true;
}

/*
 * Server receive using spinning/pinning receive
 */
void server_receiveWrapper(mpiMsg *msgBuf, int count, int source,
		int tag, MPI_Comm comm, MPI_Status *status, serverThreadState *threadState) {

	MPI_Request req;
	MPI_Irecv(msgBuf, count, mpi_message_type, source, tag, comm, &req);
	/* Spin in user space waiting for the message to arrive */
	int flag = false; // used to be a bool, but MPI_Test wants an int ptr
	while (flag == false) {
		MPI_Test(&req, &flag, status);
		/*if (server_writeLog == true) {
			writeServerLog(threadState);
		}*/
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
	if (threadState->logFile_isOpen == true) {
		int threadID = threadState->threadID;
		int serverID = threadState->serverID;
		fprintf(threadState->logFile,
				"###########################\n"
				"SERVER %d - THREAD ID: %d\n"
				"--------------------------\n"
				"Num High Priority REQUEST msgs = %llu\n"
				"Num Low Priority REQUEST msgs = %llu\n"
				"---------------------------------\n"
				"High Priority S2S Requests Stats:\n"
			        "  Minimum Time (ns) = %ld\n"
                                "  Maximum Time (ns) = %ld\n"
                                "  Mean Time (ns) = %f\n"
                                "  Std Dev Time (ns) = %f\n"
                                "  95-percentile Time (ns) = %ld\n"
                                "  99-percentile Time (ns) = %ld\n"
				"###########################\n",
				serverID, threadID, threadState->numHPReqMsgs,
                                threadState->numLPReqMsgs,
                                hdr_min(threadState->histogram),
                                hdr_max(threadState->histogram),
                                hdr_mean(threadState->histogram),
                                hdr_stddev(threadState->histogram),
                                hdr_value_at_percentile(threadState->histogram, 95),
                                hdr_value_at_percentile(threadState->histogram, 99));
		fclose(threadState->logFile);
                fclose(threadState->serverHistData);
		threadState->logFile_isOpen = false;
		printf("SERVER %d -- THREAD %d ==> DONE reporting Stats\n", serverID, threadID);
	}
}
