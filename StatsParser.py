"""
This class parses the output files of the simulation
"""

import sys, os, re 

class StatsParser:
    """
    This class parses the log files in the output directory
    and accumulates the statistics for the simulation.
    """

    def __init__(self, directory, clientThreadsPerHost):
        self.rootdir = directory
        self.logFiles = []
        self.clientStats = {}
        self.serverStats = {}
        self.clientThreadsPerHost = clientThreadsPerHost

        self.CLIENT = 0
        self.SERVER = 1

        # Maps: server rank ==> num SYNC messages received
        self.server_utilization = {'numREQmsgs':{}}

        #######################################
        ######### CLIENT log format ###########
        #######################################
        self.client_stats_log_format = r"""####################################
CLIENT (?P<rank>[0-9]*)
Final Statistics
-----------------------------
Average High Priority Request Completion Time = (?P<avgCT>[-\d\.infa]*)
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
                if (re.search("Client", filename)):
                    self.parse_stats_log(fname, self.CLIENT)
                elif (re.search("Server", filename)):
                    self.parse_stats_log(fname, self.SERVER)

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

