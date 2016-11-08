/*
 * test.c
 *
 *  Created on: Oct 21, 2016
 *      Author: sibanez
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>


#include "mpi.h"
#include "common.h"
#include "client.h"
#include "server.h"

void test_configMaps() {

	puts("Starting Test test_configMaps()");
	/*
	 * Test Parameters
	 */
	int numProcs = 8;
	int clientThreadsPerHost = 1;
	int serverThreadsPerHost = 3;
	int coresForHPThreads = 2;

//	// Used to stall so we can attach to gdb
//	int i = 0;
//	char hostname[256];
//	gethostname(hostname, sizeof(hostname));
//	printf("***********************PID %d on %s ready for attach\n", getpid(), hostname);
//	fflush(stdout);
//	while (0 == i)
//		sleep(5);


	/*
	 * Code Under Test
	 */
	int procsPerHost, numHosts;

	calcProcInfo(&procsPerHost, &numHosts, numProcs,
			clientThreadsPerHost, serverThreadsPerHost);


	rankMap = malloc(sizeof(rankEntry)*numProcs);

	int numClients = numHosts*clientThreadsPerHost;
	int numServers = numHosts; // assuming one server per host
	clientMap = malloc(sizeof(entityEntry)*numClients);
	serverMap = malloc(sizeof(serverEntry)*numServers);
	int clientProcsPerClient = 1; // single threaded client processes
	initializeEntityMap(clientMap, numClients, clientProcsPerClient);
	int numLPThreads = 	serverThreadsPerHost - coresForHPThreads;
	int numHPThreads = coresForHPThreads;
	initializeServerMap(serverMap, numServers, numLPThreads, numHPThreads);

	// set the rankMap instanceMap, client/server Maps
	configMaps(rankMap, clientMap, serverMap, numProcs, clientThreadsPerHost,
			 serverThreadsPerHost, coresForHPThreads);

	/* Create high priority / low priority communicators
	 */
	createHPComm(&highPriority_comm);
	createLPComm(&lowPriority_comm);

	/* Make sure we are not using more threads than cores */
	int numLogicalCores = sysconf( _SC_NPROCESSORS_ONLN );
	int limit = numLogicalCores/2;
	int len = 100;
    char hostname[len];
	gethostname(hostname, sizeof(hostname));
	if (strcmp(hostname, "ubuntu") != 0) {
		assert(clientThreadsPerHost + serverThreadsPerHost <= limit);
		assert(coresForHPThreads < serverThreadsPerHost);
	}

	// writing results
	char filename[100];
	sprintf(filename, "./Maps-config.log");
	FILE *fp = initLog(filename, rankMap);
	writeRankMap(fp, rankMap, numProcs);
	fprintf(fp, "----------------------\nClient Map: \n");
	writeEntityMap(fp, clientMap, numClients, clientProcsPerClient);
	fprintf(fp, "----------------------\nServer Map: \n");
	writeServerMap(fp, serverMap, numServers, numHPThreads, numLPThreads);
	fclose(fp);

	free(rankMap);
	freeEntityMap(clientMap, numClients);
	freeServerMap(serverMap, numServers);
}

int main (int argc, char **argv)
{
	MPI_Init(&argc, &argv);
	test_configMaps();
	MPI_Finalize();
}
