#!/usr/bin/env python

import sys, subprocess, socket, os
import shutil, re, shlex, getpass
import signal
from threading import Thread
from time import sleep
from StatsParser import StatsParser
from StatsAnalysis import *
import pexpect

SIM_RUN_TIME = 30
OUTPUT_DIR = "./out/"
BUILD = "./Debug"
SIM_LOGGING_TIME = 5
KEY = open('pass.txt', 'r').readline()

MPI_IMPL = 'OMPI'

def kill_sim(process):
    sleep(SIM_RUN_TIME)
    if (process.isalive()):
        print "[info] sending " + str(signal.SIGUSR1) + " to pid: " + str(process.pid)
        process.kill(signal.SIGUSR1)
    sleep(SIM_LOGGING_TIME) # wait for the sim to finish up and write results
    print "killing process"
    process.close()

# NOTE: This is the version of kill_sim used for the Popen version of the code
# def kill_sim(process):
#     sleep(SIM_RUN_TIME)
#     if (process.poll() is None):
#         print "sending " + str(signal.SIGUSR1) + " to pid: " + str(process.pid)
#         process.send_signal(signal.SIGUSR1)
#     sleep(SIM_LOGGING_TIME) # wait for the sim to finish up and write results
#     print "killing process"
#     process.terminate()


def run_sim(args, numHosts=None):
    procsPerHost = args['clientThreadsPerHost'] + args['serverThreadsPerHost']

    if (socket.gethostname() == 'ubuntu' or socket.gethostname() == 'sibanez-netfpga'):

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
                '--serverMemLoad', str(args['serverMemLoad']),
                '--serverNetLoad', str(args['serverNetLoad']),
                '--serverComputeLoad', str(args['serverComputeLoad']),
                '--clientHPReqRate', str(args['clientHPReqRate']),
                '--clientLPReqRate', str(args['clientLPReqRate']),
                '--coresForHPThreads', str(args['coresForHPThreads'])]
        print "command: \n", ' '.join(simArgs)
    else:
        # Running on actual cluster
        MPI_RUN_CMD = makeMPI_runCmd(procsPerHost)
        simArgs = MPI_RUN_CMD + \
                [BUILD+'/mpi_bcp_qos_sim',
                '--clientThreadsPerHost', str(args['clientThreadsPerHost']),
                '--serverThreadsPerHost', str(args['serverThreadsPerHost']),
                '--serverMemLoad', str(args['serverMemLoad']),
                '--serverNetLoad', str(args['serverNetLoad']),
                '--serverComputeLoad', str(args['serverComputeLoad']),
                '--clientHPReqRate', str(args['clientHPReqRate']),
                '--clientLPReqRate', str(args['clientLPReqRate']),
                '--coresForHPThreads', str(args['coresForHPThreads'])]
        print "[info] command: \n", ' '.join(simArgs)

    # Delete current contents of the output directory
    try:
        shutil.rmtree(OUTPUT_DIR)
    except:
        pass
    os.mkdir(OUTPUT_DIR)

    # Want to handle 3 scenarios:
    #   1. First time connecting, which will ask "Are you sure you want to continue connecting?"
    #   2. Just asking for password
    #   3. No password is asked for because you have a key or something.
    # code from http://linux.byexamples.com/archives/346/python-how-to-access-ssh-with-pexpect/
    ssh_newkey = "Are you sure you want to continue connecting"
    p = pexpect.spawn(simArgs[0], simArgs[1:])
    p.logfile_read = sys.stdout # only log what the child process sends back
    i = p.expect([ssh_newkey, ".*password:", pexpect.EOF, pexpect.TIMEOUT], timeout=2)
    if i == 0:
        print "\n[info] Saying yes to first time connection."
        p.sendline("yes")
        i = p.expect([ssh_newkey, ".*password:", pexpect.EOF, pexpect.TIMEOUT], timeout=2)
    if i == 1:
        print "\n[info] Providing user password."
        p.sendline(KEY)
        j = p.expect([".*denied.*", pexpect.EOF, pexpect.TIMEOUT], timeout=2)
        if j == 0:
            sys.exit("\n[ERROR] Did you fill in your password?")
        elif j == 3:
            pass
    elif i == 2:
        print "\n[info] Looks like I had the key."
        pass
    elif i == 3:
        # print "[info] No prompt seen, assuming it's ok and proceeding."
        pass

    # NOTE: To switch back to the Popen version of the code, uncomment this line
    # and the sys.stdout.write lines a few lines below, and comment the block above.
    # p = subprocess.Popen(simArgs, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print "[info] Launching sim at pid: " + str(p.pid)
    killer_thread = Thread(target = kill_sim, args = (p, ))
    killer_thread.start()
    # for line in iter(p.stdout.readline, ''):
    #     sys.stdout.write(line)
    killer_thread.join()
    while(p.isalive()):
        pass
    print "Process finished with returncode: ", p.exitstatus

    stats = StatsParser(OUTPUT_DIR)
    return stats


def run_sims_range(args, argsMap):
    """
    This function allows the user to specify a range for the parameters in the format:
    --param start:inc:end
    """

    numHosts = None
    if (socket.gethostname() == 'ubuntu' or socket.gethostname() == 'sibanez-netfpga'):
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
    for aggStats in finalAggStats:
        for key, value in aggStats.items():
            # Don't print out the huge CDF data at the end
            if key not in (['CDFData']):
                print "[info] Final Stats: " + key + ": " + str(value)
    # print "Final Aggregated Stats = \n", finalAggStats
    plotResults(finalAggStats, paramWithRange, rangeArgs, argsMap['cdfs'])

"""
val is the input command line parameter.
This function returns an int if val can be converted to an int
or a list of values if val is written in the format start:inc:end
"""
def parse_option(option, val):
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

def makeMPI_runCmd(procsPerHost):
    OMPI_RUN_CMD = ['mpirun', '-npernode', str(procsPerHost), '--hostfile', 'hostfile.ompi', '--bind-to', 'core', '--report-bindings']

    return OMPI_RUN_CMD

# def runCommand(command, working_directory='.', shell=False):
#     print '---------------------------------------'
#     print "Running command: $ ", command
#     print '---------------------------------------'
#     p = subprocess.Popen(shlex.split(command), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=working_directory, shell=shell)
#     for line in iter(p.stdout.readline, ''):
#         sys.stdout.write(line)
#     p.wait()
