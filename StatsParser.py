"""
This class parses the output files of the simulation
"""

import sys, os, re
import numpy as np
import csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from collections import deque
from numpy import ma
from matplotlib import scale as mscale
from matplotlib import transforms as mtransforms
from matplotlib.ticker import FixedFormatter, FixedLocator
import datetime

# Class used to get a nice scale zoomed in on 1 for the y-axis. Code from
# http://stackoverflow.com/questions/31147893/logarithmic-plot-of-a-cumulative-distribution-function-in-matplotlib
class CloseToOne(mscale.ScaleBase):
    name = 'close_to_one'

    def __init__(self, axis, **kwargs):
        mscale.ScaleBase.__init__(self)
        self.nines = kwargs.get('nines', 5)

    def get_transform(self):
        return self.Transform(self.nines)

    def set_default_locators_and_formatters(self, axis):
        axis.set_major_locator(FixedLocator(
                np.array([1-10**(-k) for k in range(1+self.nines)])))
        axis.set_major_formatter(FixedFormatter(
                [str(1-10**(-k)) for k in range(1+self.nines)]))


    def limit_range_for_scale(self, vmin, vmax, minpos):
        return vmin, min(1 - 10**(-self.nines), vmax)

    class Transform(mtransforms.Transform):
        input_dims = 1
        output_dims = 1
        is_separable = True

        def __init__(self, nines):
            mtransforms.Transform.__init__(self)
            self.nines = nines

        def transform_non_affine(self, a):
            masked = ma.masked_where(a > 1-10**(-1-self.nines), a)
            if masked.mask.any():
                return -ma.log10(1-a)
            else:
                return -np.log10(1-a)

        def inverted(self):
            return CloseToOne.InvertedTransform(self.nines)

    class InvertedTransform(mtransforms.Transform):
        input_dims = 1
        output_dims = 1
        is_separable = True

        def __init__(self, nines):
            mtransforms.Transform.__init__(self)
            self.nines = nines

        def transform_non_affine(self, a):
            return 1. - 10**(-a)

        def inverted(self):
            return CloseToOne.Transform(self.nines)

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
        mscale.register_scale(CloseToOne)

        for subdir, dirs, files in os.walk(self.rootdir):
            for filename in files:
                fname = os.path.join(subdir, filename)
                if (re.search("Client.*.log", filename)):
                    self.parse_stats_log(fname, self.CLIENT)
                elif (re.search("Client.*.hist", filename)):
                    self.parse_histogram_log(fname)
                elif (re.search("Server", filename)):
                    self.parse_stats_log(fname, self.SERVER)

        # Materialize single plot for each similation
        print "[info] Saving Client CDF."
        plt.xlabel("Latency (milliseconds)")
        plt.xscale('log')
        plt.yscale('close_to_one', nines=4)
        plt.ylabel("Cumulative Probability")
        # Decided to not try and print a legend
        # lgd = plt.legend(bbox_to_anchor=(1.05, 1), loc=2, borderaxespad=0.)
        plt.title("CDF")
        plt.grid(True)
        date = str(datetime.datetime.now())
        date = '_' + date[:date.find('.')].replace(' ', '_')
        # TODO: Probably want a PDF output with the same format as the others
        # pp = PdfPages("./plots/ClientCDF_" + str(self.callCount) + ".pdf")
        filename = "./plots/ClientCDF_" + date + ".png"
        plt.savefig(filename) #, bbox_extra_artists=(lgd,), bbox_inches='tight')

        plt.clf() # clear figure for next iteration

    def parse_histogram_log(self, filename):
        # To make indexing more readable
        VALUE = 0
        PERCENTILE = 1
        TOTALCOUNT = 2
        percentiles = deque()
        latencies = deque()
        with open(filename, 'rb') as csvfile:
            reader = csv.reader(csvfile, delimiter=',')
            for row in reader:
                if row[VALUE] == "Value":
                    continue

                latencies.append(float(row[VALUE]) / 1000000) #convert to milliseconds
                percentiles.append(float(row[PERCENTILE]))

        clientNum = re.search(r'\d+', filename).group(0);
        plt.plot(latencies, percentiles, label="Client " + str(clientNum))

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
