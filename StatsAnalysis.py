
import sys
import numpy as np
import csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
import datetime

"""
inputs:
    finalStats -- list of StatsParser variables from the simulations
    paramwithRange -- string of the option that had a range specified
    rangeArgs -- the arguments used in the simulation, including the range
"""
def aggregate_finalStats(finalStats):
    finalAggStats = []
    for stats in finalStats:
        aggStats = aggregateStats(stats)
        finalAggStats.append(aggStats)
    return finalAggStats

"""
inputs:
    stats -- StatsParser object: the results from one simulation

Client Stats:
- Computes the aggregate average completion time from all of the
  client processes and all of their threads, weighting by the number of samples.
- TODO: Computes the resulting variance (http://scipp.ucsc.edu/~haber/ph116C/iid.pdf)
- Compute the total Number of Samples from all clients and their threads
- Compute the average of the runtimes (and the variance)
- Compute the total number of requests from all clients and their threads
- Compute the average # requests/sec from the above two values

"""
def aggregateStats(stats):
    aggStats = {}
    (aggAvgCT, totalNumSamples) = computeAvgCT(stats.clientStats['avgCT'],
                                stats.clientStats['numSamples'])
    aggStats['avgCT'] = aggAvgCT
    aggStats['numSamples'] = totalNumSamples
    avgRunTime = np.mean(stats.clientStats['totalTime'])
    aggStats['avgRunTime'] = avgRunTime
    aggStats['varRunTime'] = np.var(stats.clientStats['totalTime'])
    totalNumReqs = sum(stats.clientStats['totalNumReqs'])
    aggStats['totalNumReqs'] = totalNumReqs
    aggStats['avgReqPerSec'] = totalNumReqs / float(avgRunTime)

    # Take min of mins
    aggStats['minCT'] = min(stats.clientStats['minCT'])

    # Take max of max
    aggStats['maxCT'] = max(stats.clientStats['maxCT'])

    # take mean of mean and std dev. TODO: Should this be weighted?
    aggStats['meanCT'] = np.mean(stats.clientStats['meanCT'])
    aggStats['stddevCT'] = np.mean(stats.clientStats['stddevCT'])

    # take worst-case times
    aggStats['ninefiveCT'] = max(stats.clientStats['ninefiveCT'])
    aggStats['ninenineCT'] = max(stats.clientStats['ninenineCT'])

    return aggStats


def computeAvgCT(avgCT_vec, numSamples_vec):
    try:
        assert(len(avgCT_vec) == len(numSamples_vec))
    except:
        print >> sys.stderr, "ERROR: len(avgCT_vec) != len(numSamples_vec)"

    # compute average completion time weighting by the number of samples
    totalNumSamples = 0
    totalavgCT = 0
    for (avgCT, numSamps) in zip(avgCT_vec, numSamples_vec):
        totalNumSamples += numSamps
        totalavgCT += avgCT*numSamps

    aggAvgCT = float(totalavgCT)/float(totalNumSamples)
    return (aggAvgCT, totalNumSamples)

"""
aggStats fields:
    aggStats['avgCT']
    aggStats['numSamples']
    aggStats['avgRunTime']
    aggStats['varRunTime']
    aggStats['totalNumReqs']
    aggStats['avgReqPerSec']
    aggStats['ninenineCT']
    aggStats['ninefiveCT']
    aggStats['meanCT']
    aggStats['stddevCT']
    aggStats['maxCT']
    aggStats['minCT']
"""
def plotResults(finalAggStats, paramWithRange, rangeArgs):
    print "paramWithRange = ", paramWithRange
    xdata = rangeArgs[paramWithRange]
    print "xdata = ", xdata

    # plot the average completion time
    makePlot(finalAggStats, paramWithRange, rangeArgs, 'avgCT',
        "Average Completion Time ($\mu$s)", 'Average Completion Time vs. ' + paramWithRange,
        './plots/Average_Completion_Time_vs_' + paramWithRange, 1e6)

    plt.cla()

    # plot the average number of requests/sec completed
    makePlot(finalAggStats, paramWithRange, rangeArgs, 'avgReqPerSec',
        "Avg Num Requests/Sec Completed (1000's)", 'Throughput vs. ' + paramWithRange,
        './plots/Throughput_vs_' + paramWithRange, 1e-3)

def makePlot(finalAggStats, paramWithRange, rangeArgs, measuredParam, ylabel, title, filename, scaleFactor=1):
    xdata = rangeArgs[paramWithRange]
    ydata = [aggStats[measuredParam]*scaleFactor for aggStats in finalAggStats]
    print measuredParam, " = ", ydata

    plt.axes([.15, .27, .75, .65])
    plt.plot(xdata, ydata, linestyle='-', marker='o')
    plt.ylabel(ylabel)
    plt.xlabel(paramWithRange)
    plt.title(title)
    plt.grid()

    text = makePlotDesc(rangeArgs)
    plt.figtext(.06, .06, text, fontsize='x-small')

    date = str(datetime.datetime.now())
    date = '_' + date[:date.find('.')].replace(' ', '_')
    filename += date
    plot_filename = filename + '.pdf'
    pp = PdfPages(plot_filename)
    pp.savefig()
    pp.close()
    print "Saved plot: ", plot_filename

    data_filename = filename + '.csv'
    # save the data in a .csv file
    dumpData(xdata, ydata, data_filename)

"""
Use the simulation parameters to create a plot description
"""
def makePlotDesc(rangeArgs):
    text = ''
    i = 0
    for (param, val) in rangeArgs.iteritems():
        text += param + '_' + str(val) if (i == 0) else \
                    '_' + param + '_' + str(val)
        i += 1

    # insert newline every PLOT_WIDTH characters
    PLOT_WIDTH = 100
    for i in range(len(text)):
        if i % PLOT_WIDTH == 0 and i != 0:
            text = text[:i] + '\n' + text[i:]
    return text

def dumpData(xdata, ydata, filename):
    print "writing data file: ", filename
    with open(filename, "w") as f:
        for (x,y) in zip(xdata, ydata):
            f.write(str(x) + ',' + str(y) + '\n')
