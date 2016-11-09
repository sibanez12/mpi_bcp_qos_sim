/*
 * server.h
 *
 *  Created on: Oct 20, 2016
 *      Author: sibanez
 */

#ifndef SERVER_H_
#define SERVER_H_

bool volatile server_writeLog;

typedef struct serverThreadState_s {
	int serverThreadsPerHost;
	int serverProcTime;
	int coresForHPThreads;
	int threadID;
	int serverID;
	bigArray *data;
	FILE *logFile;
	bool logFile_isOpen;
	unsigned long long numREQmsgs;
	unsigned long int seed;
	bool isHighPriority;
} serverThreadState;

void runServer(int serverThreadsPerHost, int serverProcessingTime,
		int coresForHPThreads);

bool server_getPriority(serverThreadState *threadState);

void server_runThread(serverThreadState *threadState);

void server_sendWrapper(mpiMsg *msgBuf, int count, MPI_Datatype mpi_message_type,
		int source, int tag, MPI_Comm comm);

void server_cleanup(serverThreadState *threadState);

void server_intHandler(int sig_num);

void server_receiveWrapper(mpiMsg *msgBuf, int count, int source,
		int tag, MPI_Comm comm, MPI_Status *status, serverThreadState *threadState);

void writeServerLog(serverThreadState *threadState);

#endif /* SERVER_H_ */
