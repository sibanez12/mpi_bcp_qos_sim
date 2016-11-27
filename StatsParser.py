"""
This class parses the output files of the simulation
"""

import sys, os, re
import numpy as np
import csv

class StatsParser:
    """
    This class parses the log files in the output directory
    and accumulates the statistics for the simulation.
    """
    def __init__(self, directory):
        self.rootdir = directory
        self.logFiles = []
        self.clientStats = {}
        self.serverStats = {}
        self.clientCDFData = {}

        self.CLIENT = 0
        self.SERVER = 1
        self.CLIENT_HIST = 2

        # Maps: server rank ==> num SYNC messages received
        self.server_utilization = {'numREQmsgs':{}}

        #######################################
        ######### CLIENT log format ###########
        #######################################
        self.client_stats_log_format = r"""####################################
CLIENT (?P<rank>[0-9]*)
Final Statistics
-----------------------------
High Priority Request Completion Stats:
  Average Time \(sec\) = (?P<avgCT>[-\d\.infa]*)
  Minimum Time \(ns\) = (?P<minCT>[\d\.]*)
  Maximum Time \(ns\) = (?P<maxCT>[\d\.]*)
  Mean Time \(ns\) = (?P<meanCT>[\d\.]*)
  Std Dev Time \(ns\) = (?P<stddevCT>[\d\.]*)
  95-percentile Time \(ns\) = (?P<ninefiveCT>[\d\.]*)
  99-percentile Time \(ns\) = (?P<ninenineCT>[\d\.]*)
Number of High Priority Request Samples = (?P<numSamples>[-\d\.]*)
-----------------------------
Total Time = (?P<totalTime>[-\d\.]*)
Total Num Requests = (?P<totalNumReqs>[-\d\.]*)
Requests/sec = (?P<reqsPerSec>[-\d\.infa]*)
#####################################"""

        self.client_config_log_format = r"""@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
CLIENT (?P<rank>[0-9]*)
Target Server = (?P<targetServer>[0-9]*)
Target Server Info:
--------------------
Host ID = (?P<targetHostID>[0-9]*)
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"""


        #######################################
        ######### SERVER log format ##########
        #######################################
        self.server_stats_log_format = r"""###########################
SERVER (?P<rank>[0-9]*) - THREAD ID: (?P<threadID>[0-9]*)
--------------------------
Num High Priority REQUEST msgs = (?P<numHPReqMsgs>[\d\.]*)
Num Low Priority REQUEST msgs = (?P<numLPReqMsgs>[\d\.]*)
###########################"""
        self.parse_results()

    def parse_results(self):
        for subdir, dirs, files in os.walk(self.rootdir):
            for filename in files:
                fname = os.path.join(subdir, filename)
                if (re.search("Client.*.log", filename)):
                    self.parse_stats_log(fname, self.CLIENT)
                elif (re.search("Client.*.hist", filename)):
                    self.parse_histogram_log(fname)
                elif (re.search("Server", filename)):
                    self.parse_stats_log(fname, self.SERVER)

    def parse_histogram_log(self, filename):
        # To make indexing more readable
        VALUE = 0
        PERCENTILE = 1
        TOTALCOUNT = 2
        percentiles = []
        latencies = []
        with open(filename, 'rb') as csvfile:
            reader = csv.reader(csvfile, delimiter=',')
            for row in reader:
                # Skip the first row
                if row[VALUE] == "Value":
                    continue

                latencies.append(float(row[VALUE]))
                percentiles.append(float(row[PERCENTILE]))

        clientNum = re.search(r'\d+', filename).group(0)
        # Save all of this simulations client's stats in a dictionary with
        # the client number as the key and the value being a tuple of lists
        self.clientCDFData[clientNum] = (latencies, percentiles)

    def parse_stats_log(self, filename, logType):
        if logType == self.CLIENT:
            log_format = self.client_stats_log_format
        elif logType == self.SERVER:
            log_format = self.server_stats_log_format
        else:
            print >> sys.stderr, "ERROR: trying to process unkown log type"
            sys.exit(1)
        with open(filename) as f:
            log = f.read()
            searchObj = re.search(log_format, log)
            numHits = 0
            while searchObj is not None:
                numHits += 1
                self.process_stats(searchObj.groupdict(), logType)
                log = log[:searchObj.start()] + log[searchObj.end():]
                searchObj = re.search(log_format, log)
            if numHits == 0:
                print >> sys.stderr, "WARNING: encountered empty log file: ", filename

    """
    Process the stats from one log_format
    """
    def process_stats(self, statsDict, logType):

        # First do error checking for the CLIENT log
        if (logType == self.CLIENT):
            # If numSamples == 0 then avg completion time and variance will be nan
            if (float(statsDict['numSamples']) == 0):
                print >> sys.stderr, "WARNING: numSamples = 0 in log. Ignoring log entry."
                return
            # If totalTime == 0 then Requests/sec will be inf
            if (float(statsDict['totalTime']) == 0):
                print >> sys.stderr, "WARNING: totalTime = 0 in log. Ignoring log entry."
                return

        for key, value in statsDict.iteritems():
            try:
                val = float(value)
            except:
                print >> sys.stderr, "Invalid value in log: ", value

            if logType == self.CLIENT:
                if key not in self.clientStats.keys():
                    self.clientStats[key] = [val]
                else:
                    self.clientStats[key].append(val)

            elif logType == self.SERVER:
                if key not in self.serverStats.keys():
                    self.serverStats[key] = [val]
                else:
                    self.serverStats[key].append(val)
