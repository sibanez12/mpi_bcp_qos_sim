/*
 * common.h
 *
 *  Created on: Oct 19, 2016
 *      Author: sibanez
 */

#ifndef COMMON_H_
#define COMMON_H_

#define DEBUG

#ifdef DEBUG
# define DEBUG_PRINT(x) printf x
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif

#define BUFSIZE 100
#define BIG_ARRAY_SIZE 20*8388608
#define BYTES_TO_READ 512
#define BYTES_TO_WRITE 512

typedef int bool;
#define True 1
#define False 0

#define INT_SIZE_BITS (sizeof(unsigned int)*8)
#define INVALID_INDEX -1

typedef struct bitVector_s {
	unsigned int *vector;
	int len; // in bits
	int numInts;
} bitVector;

typedef struct bigArray_s {
	long size;
	char array[BIG_ARRAY_SIZE];
} bigArray;

#define MSG_NITEMS 3
typedef struct mpiMsg_s {
	char message[BUFSIZE];
	int cont_index; // index so that the client knows what state to update when the ACK arrives
	int threadID;
} mpiMsg;


typedef struct rankEntry_s {
	int hostID;
	bool isClient;
	int clientID;
	bool isServer;
	int serverID;
} rankEntry;


/* Used for the clientMap, serverMap */
typedef struct entityEntry_s {
	int hostID;
	int *ranks;
	int count;
} entityEntry;

//// global variables /////
int threadSupport;
MPI_Datatype mpi_message_type;
rankEntry *rankMap;
entityEntry *clientMap;
entityEntry *serverMap;
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
		entityEntry *serverMap, int numProcs, int clientThreadsPerHost,
		int serverThreadsPerHost);

void initializeEntityMap(entityEntry *entityMap, int numEntities, int numThreads);

void freeEntityMap(entityEntry *entityMap, int numEntities);

void calcProcInfo(int *procsPerHost, int *numHosts, int numProcs,
		int clientThreadsPerHost, int serverThreadsPerHost);

/* Should change this to be able to handle arbitrary levels of QoS */
void createHPComm(MPI_Comm *highPriority_comm);

void createLPComm(MPI_Comm *lowPriority_comm);

void create_mpi_message_type(MPI_Datatype *mpi_message_type);

void create_message(mpiMsg *buf, char *message, int cont_index, int threadID);

void perform_task(bigArray *data, int procTime, unsigned long int *seed);

unsigned int readData(bigArray *data, unsigned int index, int bytesToRead);

void writeData(bigArray *data, unsigned int index, int bytesToWrite,
		unsigned long int *seed);

int max_array(int a[], int num_elements);

int hashFunc(int i);

unsigned long int myRandom(unsigned long int *seed);

/////////////////////////////////////////
///// Functions Used for Debugging //////
/////////////////////////////////////////

void printArray(int *array, int size);

void writeArray(FILE *fp, int *array, int size);

void writeRankMap(FILE *fp, rankEntry *rankMap, int numRanks);

void writeEntityMap(FILE *fp, entityEntry *entityMap, int numEntities, int numThreads);

FILE *initLog(char *filename, rankEntry *rankMap);

void printThreadSupport();

#endif /* COMMON_H_ */
