/*
 ============================================================================
 Name        : mpi_bcp_qos_sim.c
 Author      : Stephen Ibanez
 Version     :
 Copyright   : Your copyright notice
 Description :

 ============================================================================
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <getopt.h>
#include <signal.h>

#include "mpi.h"
#include "common.h"
#include "client.h"
#include "server.h"

/* Start the simulation */
void runSim(int argc, char **argv, int clientThreadsPerHost, int serverThreadsPerHost,
		int serverProcessingTime, int serverNetLoad, int clientHPReqRate,
		int clientLPReqRate, int coresForHPThreads)
{
	int  my_rank;  /* rank of process */
	int  numProcs; /* number of processes */

	int req = MPI_THREAD_SINGLE;
	MPI_Init_thread(&argc, &argv, req, &threadSupport);
	assert(threadSupport == req);

	/* Make the log record an MPI_Datatype */
	create_mpi_message_type(&mpi_message_type);

	/* find out process rank */
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

	/* find out number of processes */
	MPI_Comm_size(MPI_COMM_WORLD, &numProcs);
	DEBUG_PRINT(("MPI started %d processes\n", numProcs));

	int procsPerHost, numHosts;

	calcProcInfo(&procsPerHost, &numHosts, numProcs,
			clientThreadsPerHost, serverThreadsPerHost);


	rankMap = malloc(sizeof(rankEntry)*numProcs);

	int numClients = numHosts*clientThreadsPerHost;
	int numServers = numHosts;
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
	}

//	// Used to stall so we can attach to gdb
//    int i = 0;
//    gethostname(hostname, sizeof(hostname));
//    printf("PID %d -- rank %d -- on %s ready for attach\n", getpid(), my_rank, hostname);
//    fflush(stdout);
//    while (0 == i)
//        sleep(5);

	// -- DEBUGGING --
	if (my_rank == 0) {
		char filename[100];
		sprintf(filename, "./out/Maps-config.log");
		FILE *fp = initLog(filename, rankMap);
		writeRankMap(fp, rankMap, numProcs);
		fprintf(fp, "----------------------\nClient Map: \n");
		writeEntityMap(fp, clientMap, numClients, clientProcsPerClient);
		fprintf(fp, "----------------------\nServer Map: \n");
		writeServerMap(fp, serverMap, numServers, numHPThreads, numLPThreads);
		fclose(fp);
	}

	if (rankMap[my_rank].isClient) {
		/*
		 * Generates requests and collect statistics
		 */
		runClient(clientThreadsPerHost, clientHPReqRate, clientLPReqRate,
				numHosts);
	}
	else if (rankMap[my_rank].isServer) {
		/*
		 * Service requests
		 */
		runServer(serverThreadsPerHost, clientThreadsPerHost, serverProcessingTime,
				serverNetLoad, coresForHPThreads, numHosts);
	}
	else {
		printf("ERROR: process is not a CLIENT, SERVER...\n");
	}

	free(rankMap);
	freeEntityMap(clientMap, numClients);
	freeServerMap(serverMap, numServers);
	/* Will kill the simulation with a signal */
	while(true){
		sleep(5);
	}
	MPI_Finalize();
}


int main (int argc, char **argv)
{
	// Default parameter settings
	char *clientThreadsPerHost_s = "1";
	char *serverThreadsPerHost_s = "3";
	char *serverProcessingTime_s = "10";
	char *serverNetLoad_s = "1";
	char *clientHPReqRate_s = "1"; // number of HP messages outstanding at a time
	char *clientLPReqRate_s = "10"; // requests/second
	char *coresForHPThreads_s = "2";


	int c;

	while (1)
	{
		static struct option long_options[] =
		{
				/* These options don’t set a flag.
                   We distinguish them by their indices. */
				{"clientThreadsPerHost",      required_argument,   0, 'c'},
				{"serverThreadsPerHost",      required_argument,   0, 's'},
				{"serverProcessingTime",      required_argument,   0, 'p'},
				{"serverNetLoad",             required_argument,   0, 'n'},
				{"clientHPReqRate",           required_argument,   0, 'r'},
				{"clientLPReqRate",           required_argument,   0, 'l'},
				{"coresForHPThreads",         required_argument,   0, 'h'},
				{0, 0, 0, 0}
		};
		/* getopt_long stores the option index here. */
		int option_index = 0;
		c = getopt_long (argc, argv, "c:s:p:n:r:l:h:",
				long_options, &option_index);

		/* Detect the end of the options. */
		if (c == -1)
			break;

		switch (c)
		{
		case 0:
			/* If this option set a flag, do nothing else now. */
			if (long_options[option_index].flag != 0)
				break;
			printf ("option %s", long_options[option_index].name);
			if (optarg)
				printf (" with arg %s", optarg);
			printf ("\n");
			break;

		case 'c':
			clientThreadsPerHost_s = optarg;
			break;
		case 's':
			serverThreadsPerHost_s = optarg;
			break;
		case 'p':
			serverProcessingTime_s = optarg;
			break;
		case 'n':
			serverNetLoad_s = optarg;
			break;
		case 'r':
			clientHPReqRate_s = optarg;
			break;
		case 'l':
			clientLPReqRate_s = optarg;
			break;
		case 'h':
			coresForHPThreads_s = optarg;
			break;

		case '?':
			/* getopt_long already printed an error message. */
			return 1;

		default:
			abort ();
		}
	}


	/* Print any remaining command line arguments (not options). */
	if (optind < argc)
	{
		printf ("non-option ARGV-elements: ");
		while (optind < argc)
			printf ("%s ", argv[optind++]);
		putchar ('\n');
	}

	// Convert arguments to integers
	int clientThreadsPerHost;
	int serverThreadsPerHost;
	int serverProcessingTime;
	int serverNetLoad;
	int clientHPReqRate;
	int clientLPReqRate;
	int coresForHPThreads;

	char *ptr;
	int base = 10;
	clientThreadsPerHost = strtol(clientThreadsPerHost_s, &ptr, base);
	serverThreadsPerHost = strtol(serverThreadsPerHost_s, &ptr, base);
	serverProcessingTime = strtol(serverProcessingTime_s, &ptr, base);
	serverNetLoad = strtol(serverNetLoad_s, &ptr, base);
	clientHPReqRate = strtol(clientHPReqRate_s, &ptr, base);
	clientLPReqRate = strtol(clientLPReqRate_s, &ptr, base);
	coresForHPThreads = strtol(coresForHPThreads_s, &ptr, base);

	assert(clientThreadsPerHost > 0 && serverThreadsPerHost > 0 && serverProcessingTime >= 0 &&
			serverNetLoad >= 0 && clientHPReqRate >= 0 && clientLPReqRate >= 0 &&
			coresForHPThreads >= 0);
	assert(coresForHPThreads < serverThreadsPerHost);

	DEBUG_PRINT(("clientThreadsPerHost = %d, "
			"serverThreadsPerHost = %d, "
			"serverProcessingTime = %d, "
			"serverNetLoad = %d, "
			"clientHPReqRate = %d, "
			"clientLPReqRate = %d, "
			"coresForHPThreads = %d\n",
			clientThreadsPerHost, serverThreadsPerHost, serverProcessingTime, serverNetLoad,
			clientHPReqRate, clientLPReqRate, coresForHPThreads));

	/* Make sure the output directory exists */
	struct stat st = {0};
	if (stat("./out", &st) == -1) {
		mkdir("./out", 0700);
	}

	runSim(argc, argv, clientThreadsPerHost, serverThreadsPerHost, serverProcessingTime,
			serverNetLoad, clientHPReqRate, clientLPReqRate, coresForHPThreads);

	pthread_exit(NULL);
}
