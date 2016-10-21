/*
 * testing.c
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
	int numProcs = 4;
	int clientThreadsPerHost = 1;
	int serverThreadsPerHost = 1;
	char *serverType = "MPI_THREAD_SINGLE";


	/*
	 * Code Under Test
	 */
	int procsPerHost, numHosts;

	calcProcInfo(&procsPerHost, &numHosts, numProcs,
			clientThreadsPerHost, serverThreadsPerHost,
			serverType);


	rankMap = malloc(sizeof(rankEntry)*numProcs);

	int numClients = numHosts*clientThreadsPerHost;
	int numServers = numHosts; // assuming one server per host
	clientMap = malloc(sizeof(entityEntry)*numClients);
	serverMap = malloc(sizeof(entityEntry)*numServers);
	int clientProcsPerClient = 1; // single threaded client processes
	initializeEntityMap(clientMap, numClients, clientProcsPerClient);
	int serverProcsPerServer = (strcmp(serverType, "MPI_THREAD_SINGLE") == 0) ?
			serverThreadsPerHost : 1;
	initializeEntityMap(serverMap, numServers, serverProcsPerServer);

	// set the rankMap instanceMap, client/server Maps
	configMaps(rankMap, clientMap, serverMap, numProcs, clientThreadsPerHost,
			 serverThreadsPerHost, serverType);

	puts("Completed configMaps()");

	/*
	 * Writing results
	 */
	char filename[100];
	sprintf(filename, "Maps_config_testing.log");
	FILE *fp = initLog(filename, rankMap);
	writeRankMap(fp, rankMap, numProcs);
	fprintf(fp, "----------------------\nClient Map: \n");
	writeEntityMap(fp, clientMap, numClients, clientProcsPerClient);
	fprintf(fp, "----------------------\nServer Map: \n");
	writeEntityMap(fp, serverMap, numServers, serverProcsPerServer);
	fclose(fp);

	free(rankMap);
	freeEntityMap(clientMap, numClients);
	freeEntityMap(serverMap, numServers);
}

int main (int argc, char **argv)
{
	MPI_Init(&argc, &argv);
	test_configMaps();
	MPI_Finalize();
}
