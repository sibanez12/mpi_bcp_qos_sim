/*
 * common.c
 *
 *  Created on: Oct 19, 2016
 *      Author: sibanez
 */

#define _GNU_SOURCE
#include "mpi.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <semaphore.h>
#include <errno.h>

#include "common.h"


/////////////////////////////////////////
////////// Bit Vector Functions /////////
/////////////////////////////////////////

bitVector *createBitVector(int numBits) {
	bitVector *bitVec = malloc(sizeof(bitVector));
	int numInts = (numBits % (INT_SIZE_BITS) == 0) ? numBits/(INT_SIZE_BITS) : numBits/(INT_SIZE_BITS) + 1;
	bitVec->vector = malloc(sizeof(unsigned int)*numInts);
	bitVec->len = numBits;
	bitVec->numInts = numInts;
	/* initialize bit vector to all 1's */
	int num = 0;
	for (int i=0; i < numInts; i++) {
		bitVec->vector[i] = ~(num ^ num);
	}
	return bitVec;
}

void freeBitVector (bitVector *bitVec) {
	free(bitVec->vector);
	free(bitVec);
}

void setBit(bitVector *bitVec, int pos) {
	assert(pos < bitVec->len);
	bitVec->vector[pos/(INT_SIZE_BITS)] |= 1 << (pos % INT_SIZE_BITS);
}

void clearBit(bitVector *bitVec, int pos) {
	assert(pos < bitVec->len);
	bitVec->vector[pos/INT_SIZE_BITS] &= ~(1 << (pos % INT_SIZE_BITS));
}

bool testBit(bitVector *bitVec, int pos) {
	assert(pos < bitVec->len);
	return ( (bitVec->vector[pos/INT_SIZE_BITS] & (1 << (pos % INT_SIZE_BITS) )) != 0 );
}

/*
 * Return the index in the bitVector of the least significant 1
 * or returns INVALID_INDEX if the bitVec is all 0's
 */
int getFirstOne(bitVector *bitVec) {

	int index = 0;
	for (int i=0; i < bitVec->numInts; i++) {
		if (bitVec->vector[i] == 0) {
			index += INT_SIZE_BITS;
		} else {
			index += __builtin_ctz(bitVec->vector[i]); // count trailing zeros
		}
	}
	return (index < bitVec->len) ? index : INVALID_INDEX;
}

/////////////////////////////////////////

/*
 * Configure the Map data structures to keep track of
 * simulation topology.
 *
 * Example Setup:
 *                      Host-0
 *                      ------
 * |   Rank-0    Rank-1    Rank-2    Rank-3    Rank-4   |
 * |  client-0  client-1  client-2  server-0  server-1  |
 *
 *                      Host-1
 *                      ------
 * |   Rank-5    Rank-6    Rank-7    Rank-8    Rank-9   |
 * |  client-3  client-4  client-5  server-2  server-3  |
 *
 * This system has 2 hosts, with 3 client processes on each host,
 * and 2 server processes on each host
 *
 */
void configMaps(rankEntry *rankMap, entityEntry *clientMap,
		serverEntry *serverMap, int numProcs, int clientThreadsPerHost,
		int serverThreadsPerHost, int coresForHPThreads) {

	int procsPerHost, numHosts;

	calcProcInfo(&procsPerHost, &numHosts, numProcs,
			clientThreadsPerHost, serverThreadsPerHost);

	int rank = 0;
	int hostID = 0;
	int hostOffset;
	while (rank < numProcs) {
		hostOffset = rank % procsPerHost; //offset within the host
		if (hostOffset == 0 && rank >= procsPerHost)
			hostID++;
		int clientID = hostID*clientThreadsPerHost + hostOffset % clientThreadsPerHost;
		int serverID = hostID;

		rankMap[rank].hostID = hostID;

		bool isClient = (hostOffset < clientThreadsPerHost );
		rankMap[rank].isClient = isClient;
		clientID = (isClient) ? clientID : INVALID_INDEX;
		rankMap[rank].clientID = clientID;

		bool isServer = !isClient;
		rankMap[rank].isServer = isServer;
		rankMap[rank].serverID = (isServer) ? serverID : INVALID_INDEX ;
		bool isHPServer = (isServer && hostOffset < clientThreadsPerHost + coresForHPThreads);
		bool isLPServer = (isServer && !isHPServer);

		// set clientMap
		if (isClient) {
			clientMap[clientID].hostID = hostID;
			int index = clientMap[clientID].count;
			clientMap[clientID].ranks[index] = rank;
			clientMap[clientID].count++;
		}
		// set serverMap
		if (isHPServer) {
			serverMap[serverID].hostID = hostID;
			int HPindex = serverMap[serverID].HPcount;
			serverMap[serverID].HPranks[HPindex] = rank;
			serverMap[serverID].HPcount++;
		} else if (isLPServer) {
			serverMap[serverID].hostID = hostID;
			int LPindex = serverMap[serverID].LPcount;
			serverMap[serverID].LPranks[LPindex] = rank;
			serverMap[serverID].LPcount++;
		}

		rank++;
	}

}

void initializeEntityMap(entityEntry *entityMap, int numEntities, int numThreads) {
	for (int i=0; i < numEntities; i++) {
		entityMap[i].count = 0;
		entityMap[i].ranks = malloc(sizeof(int)*numThreads);
	}
}

void initializeServerMap(serverEntry *serverMap, int numServers, int numLPthreads, int numHPthreads) {
	for (int i=0; i < numServers; i++) {
		serverMap[i].LPcount = 0;
		serverMap[i].HPcount = 0;
		serverMap[i].LPranks = malloc(sizeof(int)*numLPthreads);
		serverMap[i].HPranks = malloc(sizeof(int)*numHPthreads);
	}
}

void freeEntityMap(entityEntry *entityMap, int numEntities) {
	for (int i=0; i < numEntities; i++) {
		free(entityMap[i].ranks);
	}
	free(entityMap);
}

void freeServerMap(serverEntry *serverMap, int numServers) {
	for (int i=0; i < numServers; i++) {
		free(serverMap[i].HPranks);
		free(serverMap[i].LPranks);
	}
	free(serverMap);
}

/*
 * Calculate:
 *     # processes per instance
 *     # processes per host
 *     # number of hosts
 */
void calcProcInfo(int *procsPerHost, int *numHosts, int numProcs,
		int clientThreadsPerHost, int serverThreadsPerHost) {

	*procsPerHost = clientThreadsPerHost + serverThreadsPerHost;
	*numHosts = numProcs/(*procsPerHost);
}

void createHPComm(MPI_Comm *highPriority_comm) {
	MPI_Comm_dup(MPI_COMM_WORLD, highPriority_comm);
}

void createLPComm(MPI_Comm *lowPriority_comm) {
	MPI_Comm_dup(MPI_COMM_WORLD, lowPriority_comm);
}

void create_mpi_message_type(MPI_Datatype *mpi_message_type) {
	/* create a type for struct car */
	int nitems = MSG_NITEMS;
	int          blocklengths[MSG_NITEMS] = {BUFSIZE, 1};
	MPI_Datatype types[MSG_NITEMS] = {MPI_CHAR, MPI_INT};
	MPI_Aint     offsets[MSG_NITEMS];

	offsets[0] = offsetof(mpiMsg, message);
	offsets[1] = offsetof(mpiMsg, threadID);

	MPI_Type_create_struct(nitems, blocklengths, offsets, types, mpi_message_type);
	MPI_Type_commit(mpi_message_type);
}

/*
 * Create a message
 */
void create_message(mpiMsg *buf, char *message, int threadID) {
	if (strlen(message) < BUFSIZE)
		strncpy(buf->message, message, BUFSIZE);
	else {
		printf("message is too long, must be less than %d bytes", BUFSIZE);
		exit(-1);
	}
	buf->threadID = threadID;
}

void perform_memory_task(bigArray *data, int memLoad, unsigned long int *seed) {
	/*
	 * Generate cache misses
	 */
	unsigned int index;
	unsigned long int randVal;
	unsigned int val;
	for (int i = 0; i < memLoad; i++) {
		randVal = myRandom(seed);
		index = (randVal + val) % (data->size);
		/* Read */
		val = readData(data, index, BYTES_TO_READ);

		randVal = myRandom(seed);
		index = (randVal + val) % (data->size);
		/* Write */
		writeData(data, index, BYTES_TO_WRITE, seed);
	}
}

/*
 * Run a computation workload for computeLoad*100 us
 */
void perform_compute_task(int computeLoad) {
	long double increment = 100000; // 100 us = 100000 ns

	struct timespec start_time;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_time);
	struct timespec curr_time;

	long double duration; // duration in nanoseconds
	struct timespec duration_ts;
	bool limitReached = false;

	double value = 12.2; // random initial floating point value;

	while (!limitReached) {
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &curr_time);
		bool isNeg = timespec_subtract(&duration_ts, &curr_time, &start_time);
		duration = timespec_to_double(&duration_ts)*NSEC_PER_SEC;
	    if (isNeg) printf("ERROR: curr_time - start_time < 0\n");
	    else if (duration > (double)computeLoad*increment) {
	    	limitReached = true;
	    } else {
	    	value += 1234567.654/value; // floating point computation
	    }
	}
}

unsigned int readData(bigArray *data, unsigned int index, int bytesToRead) {
	unsigned int result = 0;
	for (unsigned int i = index; i < (index + bytesToRead); i++) {
		result += data->array[i % data->size];
	}
	return result;
}

void writeData(bigArray *data, unsigned int index, int bytesToWrite,
		unsigned long int *seed) {
	for (unsigned int i = index; i < (index + bytesToWrite); i++) {
		data->array[i % data->size] = (char)myRandom(seed);
	}
}


int max_array(int a[], int num_elements) {
	int max = -3200;
	if (num_elements > 0) {
		max = a[0];
		for (int i = 0; i < num_elements; i++) {
			if (a[i] > max) {
				max = a[i];
			}
		}
	}
	return(max);
}


int hashFunc(int i) {
	return (i+1)*2654435761 % (1 << 30);
}

/*
 * Linear Congurential Generator formula adapted from:
 * ==>https://www.ibm.com/support/knowledgecenter/SSLTBW_2.1.0/com.ibm.zos.v2r1.bpxbd00/rernd4.htm
 */
unsigned long int myRandom( unsigned long int *seed) {
	unsigned long a = 0x5dece66d;
	unsigned long c = 0xb;
	*seed = (a*(*seed) + c) % (1 << 30);
	return (*seed);
}

/* Subtract the ‘struct timespec’ values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0. */
int timespec_subtract (struct timespec *result, struct timespec *x, struct timespec *y) {
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_nsec < y->tv_nsec) {
    int nsec = (y->tv_nsec - x->tv_nsec) / NSEC_PER_SEC + 1;
    y->tv_nsec -= NSEC_PER_SEC * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_nsec - y->tv_nsec > NSEC_PER_SEC) {
    int nsec = (x->tv_nsec - y->tv_nsec) / NSEC_PER_SEC;
    y->tv_nsec += NSEC_PER_SEC * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_nsec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_nsec = x->tv_nsec - y->tv_nsec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}

/*
 * Compute the inter packetgap based on the rate
 */
void compute_ipg(int rate_packets_per_sec, struct timespec *ipg_t) {
	double ipg_d = 1.0/((double) rate_packets_per_sec);

	// convert to nano seconds and cast to integer value
	ipg_d *= NSEC_PER_SEC;
	unsigned long ipg_nsec = (unsigned long) ipg_d;

	ipg_t->tv_sec = ipg_nsec / NSEC_PER_SEC;
	ipg_t->tv_nsec = ipg_nsec % NSEC_PER_SEC;

}

/**
 * Return the time in seconds.
 */
long double timespec_to_double(struct timespec *time) {
	return ((long double) time->tv_sec) + ((long double) time->tv_nsec/NSEC_PER_SEC);
}

/////////////////////////////////////////
///// Functions Used for Debugging //////
/////////////////////////////////////////

void printArray(int *array, int size) {
	DEBUG_PRINT(("[ "));
	for (int i=0; i < size; i++) {
		if (i != size -1)
			DEBUG_PRINT(("%d , ", array[i]));
		else
			DEBUG_PRINT(("%d ", array[i]));
	}
	DEBUG_PRINT((" ]\n"));
}

void writeArray(FILE *fp, int *array, int size) {
	fprintf(fp, "[ ");
	for (int i=0; i < size; i++) {
		if (i != size -1)
			fprintf(fp, "%d , ", array[i]);
		else
			fprintf(fp, "%d ", array[i]);
	}
	fprintf(fp, " ]\n");
}

void writeRankMap(FILE *fp, rankEntry *rankMap, int numRanks) {

	int my_rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

	DEBUG_PRINT(("About to write rankMap... Process %d\n", my_rank));

	fprintf(fp, "---------------------\nRank Map: \n");
	for (int r=0; r < numRanks; r++) {
		fprintf(fp, "rank %d:\n", r);
		fprintf(fp, "	hostID		==> %d\n", rankMap[r].hostID);
		fprintf(fp, "	isClient	==> %d\n", rankMap[r].isClient);
		fprintf(fp, "	clientID	==> %d\n", rankMap[r].clientID);
		fprintf(fp, "	isServer	==> %d\n", rankMap[r].isServer);
		fprintf(fp, "	serverID	==> %d\n", rankMap[r].serverID);
	}
	fprintf(fp, "----------------------\n");
}

void writeEntityMap(FILE *fp, entityEntry *entityMap, int numEntities, int numThreads) {
	for (int i=0; i < numEntities; i++) {
		fprintf(fp, "Entity ID -- %d:\n", i);
		fprintf(fp, "	hostID	==> %d\n", entityMap[i].hostID);
		fprintf(fp, "	ranks	==> "); writeArray(fp, entityMap[i].ranks, numThreads);
		fprintf(fp, "	count	==> %d\n", entityMap[i].count);
	}
	fprintf(fp, "----------------------\n");
}

void writeServerMap(FILE *fp, serverEntry *serverMap, int numServers, int numHPThreads,
		int numLPThreads) {
	for (int i=0; i < numServers; i++) {
		fprintf(fp, "Server ID -- %d:\n", i);
		fprintf(fp, "	hostID ==> %d\n", serverMap[i].hostID);
		fprintf(fp, "	HPranks ==> "); writeArray(fp, serverMap[i].HPranks, numHPThreads);
		fprintf(fp, "	HPcount ==> %d\n", serverMap[i].HPcount);
		fprintf(fp, "	LPranks ==> "); writeArray(fp, serverMap[i].LPranks, numLPThreads);
		fprintf(fp, "	LPcount ==> %d\n", serverMap[i].LPcount);
	}
}

FILE *initLog(char *filename, rankEntry *rankMap) {
	int my_rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

	FILE *fp = fopen(filename, "w");
	fprintf(fp, "Begin Log for Process: %d\n", my_rank);
//	fprintf(fp,
//			"Begin Log for Process: %d\n"
//			"Host ID = %d\n"
//			"isClient = %d\n"
//			"clientID = %d\n"
//			"isServer = %d\n"
//			"serverID = %d\n",
//			my_rank,
//			rankMap[my_rank].hostID,
//			rankMap[my_rank].isClient,
//			rankMap[my_rank].clientID,
//			rankMap[my_rank].isServer,
//			rankMap[my_rank].serverID);
	fclose(fp);
	fp = fopen(filename, "a");
	return fp;
}

FILE *initHistData(char *filename, rankEntry *rankMap) {
	int my_rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

	FILE *fp = fopen(filename, "w");
	fclose(fp);
	fp = fopen(filename, "a");
	return fp;
}



void printThreadSupport() {
	if (threadSupport == MPI_THREAD_SINGLE)
		printf("threadSupport = MPI_THREAD_SINGLE\n");
	else if (threadSupport == MPI_THREAD_FUNNELED)
		printf("threadSupport = MPI_THREAD_FUNNELED\n");
	else if (threadSupport == MPI_THREAD_SERIALIZED)
		printf("threadSupport = MPI_THREAD_SERIALIZED\n");
	else if (threadSupport == MPI_THREAD_MULTIPLE)
		printf("threadSupport = MPI_THREAD_MULTIPLE\n");
	else
		printf("ERROR: Unknown threadSupport provided\n");
}
