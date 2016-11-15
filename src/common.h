/*
 * common.h
 *
 *  Created on: Oct 19, 2016
 *      Author: sibanez
 */

#ifndef COMMON_H_
#define COMMON_H_

//#define DEBUG

#define DEBUG_HIST

#ifdef DEBUG
# define DEBUG_PRINT(x) printf x
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif

#ifdef DEBUG_HIST
# define DEBUG_HIST_PRINT(x) printf x
#else
# define DEBUG_HIST_PRINT(x) do {} while (0)
#endif

#define BUFSIZE 100
#define BIG_ARRAY_SIZE 20*8388608
#define BYTES_TO_READ 512
#define BYTES_TO_WRITE 512

//typedef int bool;
#define True 1
#define False 0

#include <stdbool.h>



#define INT_SIZE_BITS (sizeof(unsigned int)*8)
#define INVALID_INDEX -1

#define NSEC_PER_SEC 1000000000

#define NUM_WARMUP_SAMPLES 50

typedef struct bitVector_s {
	unsigned int *vector;
	int len; // in bits
	int numInts;
} bitVector;

typedef struct bigArray_s {
	long size;
	char array[BIG_ARRAY_SIZE];
} bigArray;

#define MSG_NITEMS 2
typedef struct mpiMsg_s {
	char message[BUFSIZE];
	int threadID;
} mpiMsg;


typedef struct rankEntry_s {
	int hostID;
	bool isClient;
	int clientID;
	bool isServer;
	int serverID;
} rankEntry;


/* Used for the clientMap */
typedef struct entityEntry_s {
	int hostID;
	int *ranks;
	int count;
} entityEntry;

typedef struct serverEntry_s {
	int hostID;
	int *HPranks;
	int HPcount;
	int *LPranks;
	int LPcount;
} serverEntry;

//// global variables /////
int threadSupport;
MPI_Datatype mpi_message_type;
rankEntry *rankMap;
entityEntry *clientMap;
serverEntry *serverMap;
MPI_Comm highPriority_comm;
MPI_Comm lowPriority_comm;
///////////////////////////

/////////////////////////////////////////
////////// Bit Vector Functions /////////
/////////////////////////////////////////

bitVector *createBitVector(int numBits);

void freeBitVector(bitVector *bitVec);

void setBit(bitVector *bitVec, int pos);

void clearBit(bitVector *bitVec, int pos);

bool testBit(bitVector *bitVec, int pos);

int getFirstOne(bitVector *bitVec);

//////////////////////////////////////////

void configMaps(rankEntry *rankMap, entityEntry *clientMap,
		serverEntry *serverMap, int numProcs, int clientThreadsPerHost,
		int serverThreadsPerHost, int coresForHPThreads);

void initializeEntityMap(entityEntry *entityMap, int numEntities, int numThreads);

void initializeServerMap(serverEntry *serverMap, int numServers, int numLPthreads, int numHPthreads);

void freeEntityMap(entityEntry *entityMap, int numEntities);

void freeServerMap(serverEntry *serverMap, int numServers);

void calcProcInfo(int *procsPerHost, int *numHosts, int numProcs,
		int clientThreadsPerHost, int serverThreadsPerHost);

/* Should change this to be able to handle arbitrary levels of QoS */
void createHPComm(MPI_Comm *highPriority_comm);

void createLPComm(MPI_Comm *lowPriority_comm);

void create_mpi_message_type(MPI_Datatype *mpi_message_type);

void create_message(mpiMsg *buf, char *message, int threadID);

void perform_task(bigArray *data, int procTime, unsigned long int *seed);

unsigned int readData(bigArray *data, unsigned int index, int bytesToRead);

void writeData(bigArray *data, unsigned int index, int bytesToWrite,
		unsigned long int *seed);

int max_array(int a[], int num_elements);

int hashFunc(int i);

unsigned long int myRandom(unsigned long int *seed);

int timespec_subtract (struct timespec *result, struct timespec *x, struct timespec *y);

void compute_ipg(int rate_packets_per_sec, struct timespec *ipg_t);

long double timespec_to_double(struct timespec *time);

/////////////////////////////////////////
///// Functions Used for Debugging //////
/////////////////////////////////////////

void printArray(int *array, int size);

void writeArray(FILE *fp, int *array, int size);

void writeRankMap(FILE *fp, rankEntry *rankMap, int numRanks);

void writeEntityMap(FILE *fp, entityEntry *entityMap, int numEntities, int numThreads);

void writeServerMap(FILE *fp, serverEntry *serverMap, int numServers, int numHPThreads,
		int numLPThreads);

FILE *initLog(char *filename, rankEntry *rankMap);

FILE *initHistData(char *filename, rankEntry *rankMap);

void printThreadSupport();

#endif /* COMMON_H_ */
