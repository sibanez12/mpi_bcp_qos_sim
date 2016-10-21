
import sys, subprocess, socket, os
import shutil, re, shlex, getpass
import signal
from threading import Thread 
from time import sleep
from StatsParser import StatsParser
from StatsAnalysis import *

SIM_RUN_TIME = 10
OUTPUT_DIR = "./out/"
BUILD = "./Debug"
SIM_LOGGING_TIME = 5

MPI_IMPL = 'OMPI'

def kill_sim(process):
    sleep(SIM_RUN_TIME)
    if (process.poll() is None):
        print "sending SIGUSR1"
        process.send_signal(signal.SIGUSR1)
    sleep(SIM_LOGGING_TIME) # wait for the sim to finish up and write results
    print "killing process"
    process.terminate()

    # process.wait()

def run_sim(args, numHosts=None):

    assert(args['serverType'] in ['MPI_THREAD_SINGLE', 'MPI_THREAD_FUNNELED', 'MPI_THREAD_MULTIPLE'])

    if (args['serverType'] == 'MPI_THREAD_SINGLE'):
        procsPerHost = args['clientThreadsPerHost'] + args['serverThreadsPerHost']
    else:
        # assuming just one server per host
        procsPerHost = args['clientThreadsPerHost'] + 1

    if (socket.gethostname() == 'ubuntu'):
        
        try:
            assert(numHosts is not None)
        except:
            print >> sys.stderr, "ERROR: numHosts must be specified to run locally"
            sys.exit(1)
        # Testing on laptop
        totalProcs = procsPerHost*numHosts
        print "total Procs = ", totalProcs
        MPI_RUN_CMD = ['mpiexec', '-n', str(totalProcs), '-env', 'MV2_SMP_USE_CMA=0', \
        '-env', 'MV2_ENABLE_AFFINITY=0'] if (MPI_IMPL == 'MVAPICH2') else ['mpirun', '-np', str(totalProcs)]
        simArgs = MPI_RUN_CMD + \
                [BUILD+'/mpi_bcp_qos_sim', 
                '--clientThreadsPerHost', str(args['clientThreadsPerHost']), 
                '--serverThreadsPerHost', str(args['serverThreadsPerHost']),
                '--serverProcessingTime', str(args['serverProcessingTime']),
                '--clientReqPerHost', str(args['clientReqPerHost']),
                '--clientReqGrpSize', str(args['clientReqGrpSize']),
                '--coresForHPThreads', str(args['coresForHPThreads']),
                '--serverType', args['serverType']]
        print "command: \n", ' '.join(simArgs)
    else:

        # Running on infiniband cluster
        MPI_RUN_CMD = makeMPI_runCmd(procsPerHost)
        simArgs = MPI_RUN_CMD + \
                [BUILD+'/mpi_bcp_qos_sim',
                '--clientThreadsPerHost', str(args['clientThreadsPerHost']), 
                '--serverThreadsPerHost', str(args['serverThreadsPerHost']),
                '--serverProcessingTime', str(args['serverProcessingTime']),
                '--clientReqPerHost', str(args['clientReqPerHost']),
                '--clientReqGrpSize', str(args['clientReqGrpSize']),
                '--coresForHPThreads', str(args['coresForHPThreads']),
                '--serverType', args['serverType']]
        print "command: \n", ' '.join(simArgs)

    # Delete current contents of the output directory 
    try:
        shutil.rmtree(OUTPUT_DIR)
    except:
        pass
    os.mkdir(OUTPUT_DIR)

    p = subprocess.Popen(simArgs, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    killer_thread = Thread(target = kill_sim, args = (p, ))
    killer_thread.start()
    for line in iter(p.stdout.readline, ''):
        sys.stdout.write(line)
    killer_thread.join()
    p.wait()
    print "Process finished with returncode: ", p.returncode

    stats = StatsParser(OUTPUT_DIR, args['clientReqPerHost'], args['clientThreadsPerHost'])
    return stats


def run_sims_range(args, argsMap):
    """
    This function allows the user to specify a range for the parameters in the format:
    --param start:inc:end
    """

    numHosts = None
    if (socket.gethostname() == 'ubuntu'):
        while (numHosts is None):
            numHosts_s = raw_input("Enter the number of hosts to simulate: ")
            try:
                numHosts = int(numHosts_s)
            except:
                numHosts = None

    # Maps strings of options to values, or list of values
    rangeArgs = {}
    for option, val in argsMap.iteritems():
        rangeArgs[option] = parse_option(option, val)

    # list of options that have ranges specified
    optionsWithRange = findRangeOptions(rangeArgs)

    # NOTE: for now only support one range option at a time!!
    try:
        assert(len(optionsWithRange) == 1)
    except:
        print >> sys.stderr, "ERROR: exactly one parameter is allowed to have an specified range. You specified: ", len(optionsWithRange)
        sys.exit(1)

    finalStats = []
    paramWithRange = optionsWithRange[0]
    print paramWithRange, " ==> ", rangeArgs[paramWithRange]
    for val in rangeArgs[paramWithRange]:
        sim_args = rangeArgs.copy()
        sim_args[paramWithRange] = val
        stats = run_sim(sim_args, numHosts=numHosts)
        finalStats.append(stats)

    if not os.path.exists('./plots'):
        os.makedirs('./plots')

    print "All Simulations completed!"
    finalAggStats = aggregate_finalStats(finalStats)
    print "Final Aggregated Stats = \n", finalAggStats
    plotResults(finalAggStats, paramWithRange, rangeArgs)

"""
val is the input command line parameter.
This function returns an int if val can be converted to an int
or a list of values if val is written in the format start:inc:end
"""
def parse_option(option, val):
    if (option == "serverType"):
        return val
    try:
        result = int(val)
    except:
        regex = r"(?P<start>[0-9]*):(?P<inc>[0-9]*):(?P<end>[0-9]*)"
        searchObj = re.match(regex, val)
        if (searchObj is not None):
            dic = searchObj.groupdict()
            result = range(int(dic['start']), int(dic['end']), int(dic['inc']))
        else:
            print >> sys.stderr, "ERROR: Invalid command line parameter: ", val
    return result

"""
Find the options that have ranges specified
"""
def findRangeOptions(rangeArgs):
    result = []
    for option, val in rangeArgs.iteritems():
        if isinstance(val, list):
            result.append(option)
    return result

"""
Print a summay of the topology and the utilization for each experiment
"""
def printExperimentSummaries(finalStats, printUtilization):
    i = 0
    for stats in finalStats:
        print "##########################"
        print "Summary for experiment %d:" % i
        print "##########################"
        stats.printExpSummary(printUtilization)
        i += 1

def gethostnames():
    with open('hostnames') as f:
        hostnames = f.readlines()

    hosts = []
    for h in hostnames:
        hosts.append(h[:-1])
    return hosts

"""
Make the hostfile used to start the MPI job
"""
def makeHostFile(procsPerHost):
    hosts = gethostnames() 

    with open('hosts', 'w') as f:
        for host in hosts:
            f.write(host + ':' + str(procsPerHost) + '\n')

def makeMPI_runCmd(procsPerHost):
    OMPI_RUN_CMD = ['mpirun', '--mca', 'btl_openib_warn_default_gid_prefix', '0',
    '--mca', 'btl', '^tcp', '-npernode', str(procsPerHost), '--hostfile', 'hostfile.ompi', '--bind-to', 'none']

    makeHostFile(procsPerHost)
    MVAPICH2_RUN_CMD = ['mpiexec', '-f', 'hosts', '-env', 'MV2_SMP_USE_CMA=0', '-env', 'MV2_ENABLE_AFFINITY=0']

    return MVAPICH2_RUN_CMD if (MPI_IMPL == 'MVAPICH2') else OMPI_RUN_CMD

def runCommand(command, working_directory='.', shell=False):
    print '---------------------------------------'
    print "Running command: $ ", command
    print '---------------------------------------'
    p = subprocess.Popen(shlex.split(command), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=working_directory, shell=shell)
    for line in iter(p.stdout.readline, ''):
        sys.stdout.write(line)
    p.wait()