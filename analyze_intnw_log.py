#!/usr/bin/env python

'''
# Things I want this tool to show:
# 1) Application data that's sent or received
#    a) With annotations: IROB numbers, data size, dependencies
# 2) Which network data is transferred on
# 3) When data is acknowledged (IntNW ACK)
# 4) When data is retransmitted
# 5) When networks come and go
# 6) (Maybe) IntNW control messages


# Visualization ideas:
# 1) Which network: a vertical section of the plot for each network
# 2) Sending app data: solid colored bar
# 3) Waiting for ACK: empty bar with solid outline
# 4) Sent vs. received app data: two vertical subsections (send/recv)
#    in each network's vertical section
# 5) Network coming and going: shaded section, same as for IMP
# 6) Annotations: popup box on hover (PyQt)

# Borrows heavily from
#   http://eli.thegreenplace.net/2009/01/20/matplotlib-with-pyqt-guis/

'''

import sys, re, os
from argparse import ArgumentParser

from PyQt4.QtCore import *
from PyQt4.QtGui import *

import matplotlib
from matplotlib.backends.backend_qt4agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.backends.backend_qt4agg import NavigationToolbar2QTAgg as NavigationToolbar
from matplotlib.figure import Figure

import numpy as np
import scipy.stats as stats

from itertools import product
from bisect import bisect_right
from copy import copy

from progressbar import ProgressBar

sys.path.append(os.getenv("HOME") + "/scripts/nistnet_scripts/traces")
import mobility_trace
from mobility_trace import NetworkChooser

debug = False
def dprint(msg):
    if debug:
        print msg

class LogParsingError(Exception):
    def __init__(self, msg):
        self.msg = msg

    def setLine(self, linenum, line):
        self.linenum = linenum
        self.line = line

    def __repr__(self):
        return "LogParsingError: " + str(self)

    def __str__(self):
        return ("IntNW log parse error at line %s: %s%s"
                % (self.linenum and str(self.linenum) or "<unknown>",
                   self.msg,
                   self.line != None and ('\n' + self.line) or ""))

class IROBError(Exception):
    pass

def stepwise_mean(data):
    means = []
    mean = 0.0
    n = 0
    for value in data:
        n += 1
        mean = update_running_mean(mean, value, n)
        means.append(mean)
        
    return means

def stepwise_variance(data):
    '''Returns a list with the step-by-step variance computed on data.
    Algorithm borrowed from
    http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#On-line_algorithm
    which cites Knuth's /The Art of Computer Programming/, Volume 1.

    '''
    n = 0
    mean = 0.0
    M2 = 0.0
    variances = [0.0]
    
    for x in data:
        n += 1
        delta = x - mean
        mean += delta / n
        M2 += delta * (x - mean)
        
        if n > 1:
            variances.append(M2 / (n - 1))

    assert len(variances) == len(data)
    return variances

def update_running_mean(mean, value, n):
    return mean + ((value - mean) / (n+1))

def alpha_to_percent(alpha):
    return 100.0 * (1 - alpha)

def percent_to_alpha(percent):
    return 1.0 - (percent / 100.0)
    
# http://code.activestate.com/recipes/577219-minimalistic-memoization/
def memoize(f):
    cache = {}
    def memf(*args):
        if args not in cache:
            cache[args] = f(*args)
        return cache[args]
    return memf

@memoize
def get_t_value(alpha, df):
    'for two-sided confidence interval.'
    return stats.t.ppf(1 - alpha/2.0, df)

def confidence_interval(alpha, stddev, n):
    t = get_t_value(alpha, n-1)
    return t * stddev / (n ** 0.5)


def shift_right_by_one(l):
    if len(l) > 0:
        l = [l[0]] + l[:-1]
    return l

def init_dict(my_dict, key, init_value):
    if key not in my_dict:
        my_dict[key] = init_value


RELATIVE_ERROR = True

def absolute_error(prev_estimate, cur_observation):
    return prev_estimate - cur_observation

def absolute_error_adjusted_estimate(estimate, error):
    return estimate - error

def relative_error(prev_estimate, cur_observation):
    return cur_observation / prev_estimate

def relative_error_adjusted_estimate(estimate, error):
    return estimate * error

def error_value(prev_estimate, cur_observation):
    err = relative_error if RELATIVE_ERROR else absolute_error
    return err(prev_estimate, cur_observation)

def error_adjusted_estimate(estimate, error):
    adjuster = (relative_error_adjusted_estimate if RELATIVE_ERROR
                else absolute_error_adjusted_estimate)
    return adjuster(estimate, error)

def get_error_values(observations, estimated_values):
    shifted_estimates = shift_right_by_one(estimated_values)
    return [error_value(est, obs) for obs, est in zip(observations, shifted_estimates)]

def get_value_at(timestamps, values, time):
    '''Assuming zip(timestamps, values) is a list of periods
    described (somehow) by the corresponding values,
    return the value in effect at time 'time'.
    A value is 'in effect' from timestamps[i] until timestamps[i+1].

    Assumes timestamps is a sorted list.

    '''
    pos = bisect_right(timestamps, time)
    assert pos != 0 # timestamp must occur after first observation
    return values[pos - 1]

def network_metric_pairs():
    network_types = ['wifi', '3G']
    metrics = ['bandwidth_up', 'latency']
    for n, m in product(network_types, metrics):
        yield n, m


class IROB(object):
    def __init__(self, plot, network_type, direction, start, irob_id):
        self.__plot = plot
        self.network_type = network_type
        self.direction = direction
        self.irob_id = irob_id
        
        self.__start = start
        self.__datalen = None
        self.__expected_bytes = None
        self.__completion_time = None
        self.__drop_time = None
        self.__last_activity = None
        self.__acked = False

        # if true, mark as strange on plot
        self.__abnormal_end = False

        self.__checkIfComplete(start)

    def addBytes(self, timestamp, bytes):
        if self.__datalen == None:
            self.__datalen = 0
        self.__datalen += bytes
        self.__checkIfComplete(timestamp)

    def finish(self, timestamp, expected_bytes):
        self.__expected_bytes = expected_bytes
        self.__checkIfComplete(timestamp)

    def ack(self, timestamp):
        self.__acked = True
        self.__checkIfComplete(timestamp)

    def complete(self):
        # XXX: HACK.  Can be double-counting sent/received app data.
        # TODO: fix it.  maybe still keep track of duplicate data.
        return (self.__acked and
                (self.__datalen != None and
                 (self.direction == "up" or
                  self.__datalen >= self.__expected_bytes)))

    def getSize(self):
        if not self.__datalen:
            raise IROBError("expected to find IROB size")
        return self.__datalen
     
    def __checkIfComplete(self, timestamp):
        self.__last_activity = timestamp
        if self.complete():
            self.__completion_time = timestamp

    def markDropped(self, timestamp):
        if self.__drop_time == None:
            dprint("Dropped %s at %f" % (self, timestamp))
            self.__drop_time = timestamp

    def wasDropped(self):
        return bool(self.__drop_time)

    def getStart(self):
        return self.__start

    def getTimeInterval(self):
        if self.complete():
            return (self.__start, self.__completion_time)
        else:
            if self.__acked:
                end = self.__last_activity
            else:
                if self.__drop_time != None:
                    end = self.__drop_time
                else:
                    end = self.__last_activity
                    self.__abnormal_end = True
                    
            return (self.__start, end)

    def getDuration(self):
        interval = self.getTimeInterval()
        return interval[1] - interval[0]

    def __str__(self):
        return ("{IROB: id %d  direction: %s  network: %s}"
                % (self.irob_id, self.direction, self.network_type))

    def draw(self, axes):
        ypos = self.__plot.getIROBPosition(self)
        yheight = self.__plot.getIROBHeight(self)
        start, finish = [self.__plot.getAdjustedTime(ts) for ts in self.getTimeInterval()]
        dprint("%s %f--%f" % (self, start, finish))
        axes.broken_barh([[start, finish-start]],
                         [ypos - yheight / 2.0, yheight],
                         color=self.__plot.getIROBColor(self))

        if self.__drop_time != None:
            axes.plot([finish], [ypos], marker='x', color='black', markeredgewidth=2.0)
        elif self.__abnormal_end:
            axes.plot([finish], [ypos], marker='*', color='black')

timestamp_regex = re.compile("^\[([0-9]+\.[0-9]+)\]")
choose_network_time_regex = re.compile("  took ([0-9.]+) seconds")

def getTimestamp(line):
    return float(re.search(timestamp_regex, line).group(1))
            

class IntNWBehaviorPlot(QDialog):
    CONFIDENCE_ALPHA = 0.10
    
    def __init__(self, run, start, measurements_only, network_trace_file,
                 bandwidth_measurements_file, cross_country_latency, error_history, 
                 is_server, parent=None):
        QDialog.__init__(self, parent)

        self.__initRegexps()
        self.__run = run
        self.__networks = {} # {network_type => {direction => {id => IROB} } }
        self.__network_periods = {}
        self.__network_type_by_ip = {}
        self.__network_type_by_sock = {}
        self.__placeholder_sockets = {} # for when connection begins before scout msg
        self.__error_history = error_history
        self.__is_server = is_server

        self.__measurements_only = measurements_only
        self.__network_trace_file = network_trace_file
        self.__bandwidth_measurements_file = bandwidth_measurements_file
        self.__cross_country_latency = cross_country_latency
        self.__trace = None

        # estimates[network_type][bandwidth_up|latency] -> [values]
        self.__estimates = {'wifi': {'bandwidth_up': [], 'latency': []},
                            '3G': {'bandwidth_up': [], 'latency': []}}

        # second axes to plot times on
        self.__session_axes = None
        self.__user_set_max_trace_duration = None
        self.__user_set_max_time = None

        self.__alpha = IntNWBehaviorPlot.CONFIDENCE_ALPHA

        # app-level sessions from the trace_replayer.log file
        self.__sessions = []
        self.__debug_sessions = [] # sessions to highlight on the plot for debugging

        # redundancy decision calculations from instruments.log
        self.__redundancy_decisions = []

        self.__irob_height = 0.25
        self.__network_pos_offsets = {'wifi': 1.0, '3G': -1.0}
        self.__direction_pos_offsets = {'down': self.__irob_height / 2.0,
                                        'up': -self.__irob_height / 2.0}

        self.__irob_colors = {'wifi': 'blue', '3G': 'red'}

        self.__choose_network_calls = []

        self.__start = start

        # TODO: infer plot title from file path
        client_or_server = "server-side" if self.__is_server else "client-side"
        self.__title = ("IntNW %s - Run %d" % 
                        (client_or_server, self.__run))

        self.create_main_frame()

    def setSessions(self, sessions):
        self.__sessions = sessions

    def setRedundancyDecisions(self, redundancy_decisions):
        self.__redundancy_decisions = redundancy_decisions
        
    def create_main_frame(self):
        self.__frame = QWidget()
        self.__dpi = 100
        self.__figure = Figure((5,4), self.__dpi) # 5" x 4"
        self.__canvas = FigureCanvas(self.__figure)
        self.__canvas.setParent(self.__frame)

        self.__mpl_toolbar = NavigationToolbar(self.__canvas, self.__frame)

        #
        # Layout with box sizers
        # 
        hbox = QHBoxLayout()

        if self.__measurements_only:
            self.__setupMeasurementWidgets(hbox)
        else:
            self.__setupActivityWidgets(hbox)
            
        vbox = QVBoxLayout(self)
        vbox.addWidget(self.__canvas, stretch=100)
        vbox.addWidget(self.__mpl_toolbar, stretch=1)
        vbox.addLayout(hbox, stretch=1)

    def __setupActivityWidgets(self, hbox):
        self.__show_sessions = QCheckBox("Session times")
        self.__show_decisions = QCheckBox("Redundancy decisions")
        self.__show_debugging = QCheckBox("Debugging")
        checks = [self.__show_sessions, self.__show_decisions,
                  self.__show_debugging]

        left_box = QVBoxLayout()
        for check in checks:
            if check is not self.__show_debugging:
                check.setChecked(True)
            left_box.addWidget(check)
            self.connect(check, SIGNAL("stateChanged(int)"), self.on_draw)

        hbox.addLayout(left_box, stretch=1)

        self.__max_trace_duration = QLineEdit("")
        self.connect(self.__max_trace_duration, SIGNAL("returnPressed()"), 
                     self.updateMaxTraceDuration)

        labeled_input = QVBoxLayout()
        labeled_input.addWidget(QLabel("Max trace duration"))
        labeled_input.addWidget(self.__max_trace_duration)
        hbox.addLayout(labeled_input, stretch=1)

        self.__max_time = QLineEdit("")
        self.connect(self.__max_time, SIGNAL("returnPressed()"), self.updateMaxTime)

        labeled_input = QVBoxLayout()
        labeled_input.addWidget(QLabel("Max time"))
        labeled_input.addWidget(self.__max_time)
        hbox.addLayout(labeled_input, stretch=1)


    def __updateUserSetField(self, field, attrname):
        try:
            value = float(field.text())
            print "Setting %s to %f" % (attrname, value)
            setattr(self, attrname, value)
            self.on_draw()
        except ValueError:
            pass

    def updateMaxTraceDuration(self):
        self.__updateUserSetField(self.__max_trace_duration,
                                  "_IntNWBehaviorPlot__user_set_max_trace_duration")

    def updateMaxTime(self):
        self.__updateUserSetField(self.__max_time, "_IntNWBehaviorPlot__user_set_max_time")

    def updateAlpha(self):
        try:
            alpha = percent_to_alpha(float(self.__ci_percent.text()))
            self.__alpha = alpha
            self.on_draw()
        except ValueError:
            pass

    def __setupMeasurementWidgets(self, hbox):
        self.__show_wifi = QCheckBox("wifi")
        self.__show_threeg = QCheckBox("3G")
        self.__show_measurements = QCheckBox("Measurements")
        self.__show_trace = QCheckBox("Trace display")
        self.__show_trace_variance = QCheckBox("Trace variance (std-dev)")
        self.__show_legend = QCheckBox("Legend")
        self.__show_decisions = QCheckBox("Redundancy decisions")
        checks = [self.__show_wifi, self.__show_threeg,
                  self.__show_measurements,
                  self.__show_trace, self.__show_trace_variance,
                  self.__show_legend, self.__show_decisions]

        networks = QVBoxLayout()
        networks.addWidget(self.__show_wifi)
        networks.addWidget(self.__show_threeg)
        hbox.addLayout(networks)

        options = QVBoxLayout()
        options.addWidget(self.__show_measurements)
        options.addWidget(self.__show_trace)
        options.addWidget(self.__show_trace_variance)
        options.addWidget(self.__show_legend)
        options.addWidget(self.__show_decisions)
        hbox.addLayout(options)

        for check in checks:
            check.setChecked(True)
            self.connect(check, SIGNAL("stateChanged(int)"), self.on_draw)

        error_plot_method_label = QLabel("Error plotting")
        self.__plot_error_bars = QRadioButton("Mean error bars")
        self.__plot_colored_error_regions = QRadioButton("Colored regions")
        
        self.__error_error_ci = \
            QRadioButton("%d%% CI" % alpha_to_percent(IntNWBehaviorPlot.CONFIDENCE_ALPHA))
        self.__error_error_stddev = QRadioButton("std-dev")

        self.__plot_colored_error_regions.setChecked(True)
        self.__error_error_ci.setChecked(True)
        
        self.connect(self.__plot_error_bars, SIGNAL("toggled(bool)"), self.on_draw)
        self.connect(self.__plot_colored_error_regions, SIGNAL("toggled(bool)"), self.on_draw)
        self.connect(self.__error_error_ci, SIGNAL("toggled(bool)"), self.on_draw)
        self.connect(self.__error_error_stddev, SIGNAL("toggled(bool)"), self.on_draw)

        percent = alpha_to_percent(IntNWBehaviorPlot.CONFIDENCE_ALPHA)
        self.__ci_percent = QLineEdit("%d" % percent)
        self.__ci_percent.setFixedWidth(80)
        self.connect(self.__ci_percent, SIGNAL("returnPressed()"), self.updateAlpha)

        error_toggles = QVBoxLayout()
        error_toggles.addWidget(self.__plot_error_bars)
        error_toggles.addWidget(self.__plot_colored_error_regions)

        error_box = QGroupBox()
        error_box.setLayout(error_toggles)

        error_interval_toggles = QVBoxLayout()
        ci_toggle = QHBoxLayout()
        ci_toggle.addWidget(self.__error_error_ci)
        ci_toggle.addWidget(self.__ci_percent)
        ci_toggle.addWidget(QLabel("%"))
        error_interval_toggles.addLayout(ci_toggle)
        error_interval_toggles.addWidget(self.__error_error_stddev)

        self.__error_interval_box = QGroupBox()
        self.__error_interval_box.setLayout(error_interval_toggles)

        all_error_toggles = QHBoxLayout()
        all_error_toggles.addWidget(error_box)
        all_error_toggles.addWidget(self.__error_interval_box)

        hbox.addLayout(all_error_toggles)

        self.__bandwidth_up_toggle = QRadioButton("Bandwidth (up)")
        self.__latency_toggle = QRadioButton("Latency")
        self.__tx_time_toggle = QRadioButton("Predicted transfer time (up)")
        
        self.__latency_toggle.setChecked(True)
        self.connect(self.__bandwidth_up_toggle, SIGNAL("toggled(bool)"), self.on_draw)
        self.connect(self.__latency_toggle, SIGNAL("toggled(bool)"), self.on_draw)
        self.connect(self.__tx_time_toggle, SIGNAL("toggled(bool)"), self.on_draw)

        toggles = QVBoxLayout()
        toggles.addWidget(self.__bandwidth_up_toggle)
        toggles.addWidget(self.__latency_toggle)
        toggles.addWidget(self.__tx_time_toggle)

        toggle_box = QGroupBox()
        toggle_box.setLayout(toggles)
        hbox.addWidget(toggle_box)
        
    def on_draw(self):
        self.setWindowTitle(self.__title)

        self.__figure.clear()
        self.__axes = self.__figure.add_subplot(111)
        self.__axes.set_title(self.__title)
        self.__session_axes = None
        
        if self.__measurements_only:
            self.__plotTrace()
            if self.__bandwidth_measurements_file:
                self.__plotActiveMeasurements()
            else:
                self.__plotMeasurements()

            self.__axes.set_xlabel("Time (seconds)")
            self.__axes.set_ylabel(self.__getYAxisLabel())
            if self.__show_legend.isChecked():
                self.__axes.legend()

            self.__drawRedundancyDecisions()

            self.__axes.set_ylim(0.0, self.__axes.get_ylim()[1])
            self.__drawWifi()
        else:
            self.__setupAxes()
            self.__setTraceEnd()
            self.__drawIROBs()
            self.__drawSessions()
            self.__drawRedundancyDecisions()

            max_trace_duration = self.__session_axes.get_xlim()[1]
            max_time = self.__session_axes.get_ylim()[1]
            if self.__user_set_max_trace_duration:
                max_trace_duration = self.__user_set_max_trace_duration
            if self.__user_set_max_time:
                max_time = self.__user_set_max_time
            self.__session_axes.set_xlim(0.0, max_trace_duration)
            self.__session_axes.set_ylim(0.0, max_time)

            if self.__show_debugging.isChecked():
                self.__drawDebugging()

            self.__drawWifi()
        
        #self.__axes.set_xlim(-100, 1300) # hack, but I'm tired of it bouncing around.

        self.__canvas.draw()

    def saveErrorTable(self):
        for network_type, metric in network_metric_pairs():
            filename = ("/tmp/%s_%s_%s_error_table_%d.txt"
                        % ("server" if self.__is_server else "client",
                           network_type, metric, self.__run))
            f = open(filename, "w")
            f.write("Time,Observation,Prev estimate,New estimate,Error\n")
            
            cur_times, observations, estimated_values = \
                self.__getAllEstimates(network_type, metric)
            shifted_estimates = shift_right_by_one(estimated_values)
            error_values = get_error_values(observations, estimated_values)

            for values in zip(cur_times, observations, 
                              shifted_estimates, estimated_values, error_values):
                f.write(",".join(str(v) for v in values) + "\n")
                
            f.close()
            

    def __whatToPlot(self):
        if self.__bandwidth_up_toggle.isChecked():
            return 'bandwidth_up'
        elif self.__latency_toggle.isChecked():
            return 'latency'
        elif self.__tx_time_toggle.isChecked():
            return 'tx_time'
        else: assert False

    def __getYAxisLabel(self):
        labels = {'bandwidth_up': 'Bandwidth (up) (bytes/sec)',
                  'latency': 'RTT (seconds)',
                  'tx_time': 'Transfer time (up) (seconds)'}
        return labels[self.__whatToPlot()]

    class NetworkTrace(object):
        def __init__(self, trace_file, window, estimates, is_server):
            self.__window = window
            self.__priv_trace = mobility_trace.NetworkTrace(trace_file)
            
            value_names = ['bandwidth_up', 'latency']
            
            field_group_indices = {'wifi': 1, '3G': 4}
        
            # XXX: assuming bandwidth-up.  not the case on the server side.
            field_offsets = {'bandwidth_up': 1, 'latency': 2}

            self.__start = None

            self.__timestamps = {'wifi': {'bandwidth_up': [], 'latency': []},
                                 '3G': {'bandwidth_up': [], 'latency': []},}
            self.__values = {'wifi': {'bandwidth_up': [], 'latency': []},
                             '3G': {'bandwidth_up': [], 'latency': []},}

            conversions = {'bandwidth_up': 1.0, 'latency': 1.0 / 1000.0}

            def getTraceKey(net_type, value_type):
                net_type = net_type.replace("3G", "cellular")
                value_type = value_type.replace("bandwidth_", "").replace("latency", "RTT")
                if is_server:
                    # "upstream" bandwidth is server->client, so it's the 'downstream'
                    #  bandwidth as recorded in the trace
                    value_type = value_type.replace("up", "down")
                return ("%s_%s" % (net_type, value_type))

            last_estimates = {}
            for network_type, what_to_plot in network_metric_pairs():
                cur_estimates = estimates[network_type][what_to_plot]
                if len(cur_estimates) > 0:
                    last_estimates[network_type] = cur_estimates[-1]['timestamp']

            for network_type, what_to_plot in network_metric_pairs():
                key = getTraceKey(network_type, what_to_plot)
                if False: #network_type in last_estimates:
                    last_estimate_time = \
                        window.getAdjustedTime(last_estimates[network_type])
                else:
                    last_estimate_time = 1200.0 # XXX: hardcoding hack.

                times = self.__priv_trace.getData('start', 0, last_estimate_time)
                values = self.__priv_trace.getData(key, 0, last_estimate_time)

                def convert(value):
                    value = conversions[what_to_plot] * value
                    if what_to_plot == "latency":
                        value = self.__window.getAdjustedTraceLatency(value)
                    return value
                
                self.__start = times[0]
                self.__timestamps[network_type][what_to_plot] = \
                    map(lambda x: x-self.__start, times)
                self.__values[network_type][what_to_plot] = map(convert, values)

            self.__computeVariance()

            
        def __computeVariance(self):
            # values dictionary is populated in __init__
            self.__upper_variance_values = {}
            self.__lower_variance_values = {}
            self.__variance_values = {}
            
            for network_type, what_to_plot in network_metric_pairs():
                values = self.__values[network_type][what_to_plot]
                variances = stepwise_variance(values)
                std_devs = [v ** .5 for v in variances]

                uppers = [v + stddev for v, stddev in zip(values, std_devs)]
                lowers = [v - stddev for v, stddev in zip(values, std_devs)]

                if network_type not in self.__upper_variance_values:
                    self.__upper_variance_values[network_type] = {}
                    self.__lower_variance_values[network_type] = {}
                    self.__variance_values[network_type] = {}
                self.__upper_variance_values[network_type][what_to_plot] = uppers
                self.__lower_variance_values[network_type][what_to_plot] = lowers
                self.__variance_values[network_type][what_to_plot] = std_devs

        def addTransfers(self, transfers):
            '''Add transfers to the trace for the purpose of
            plotting their transfer times.

            transfers -- [(start_time, size),...]   (upstream transmissions only).
            '''
            
            self.__transfers = transfers

            class SingleNetwork(NetworkChooser):
                def __init__(self, network_type):
                    types = {'wifi': NetworkChooser.WIFI, '3G': NetworkChooser.CELLULAR}
                    self.network_type = types[network_type]
                    
                def chooseNetwork(self, duration, bytelen):
                    return self.network_type

            network_choosers = dict([(type_name, SingleNetwork(type_name)) 
                                     for type_name in self.__values.keys()])

            for network_type in self.__values:
                network_chooser = network_choosers[network_type]
                tx_results = [self.__priv_trace.timeToUpload(network_chooser, *txfer)
                              for txfer in self.__transfers]

                def get_time(tx_result):
                    if (tx_result.getNetworkUsed() == network_chooser.network_type and
                        (tx_result.getNetworkUsed() == NetworkChooser.CELLULAR or
                         tx_result.cellular_recovery_start is None)):
                        return tx_result.tx_time
                    else:
                        return 0.0
                    
                times = map(get_time, tx_results)
                self.__timestamps[network_type]['tx_time'] = [t[0] for t in self.__transfers]
                self.__values[network_type]['tx_time'] = times

            # redo these calculations.  XXX: is this stdev calculation
            #  still appropriate?  probably not.
            self.__computeVariance()

        plot_colors = {'wifi': (.7, .7, 1.0), '3G': (1.0, .7, .7)}

        # whiten up the colors for the variance plotting
        variance_colors = dict([(name, color + (0.25,)) for name, color in plot_colors.items()])

        def __ploteach(self, plotter, checks):
            for network_type in self.__timestamps:
                if (checks[network_type].isChecked()):
                    plotter(network_type)
                
        def __plot(self, axes, what_to_plot, checks, values, colors, labeler):
            def plotter(network_type):
                axes.plot(self.__timestamps[network_type][what_to_plot], 
                          self.__values[network_type][what_to_plot],
                          color=colors[network_type],
                          label=labeler(network_type))
            
            self.__ploteach(plotter, checks)

        def plot(self, axes, what_to_plot, checks):
            self.__plot(axes, what_to_plot, checks, self.__values,
                        type(self).plot_colors,
                        lambda network_type: network_type + " trace")

        def plotVariance(self, axes, what_to_plot, checks):
            def plotter(network_type):
                if (what_to_plot in self.__values[network_type] and
                    what_to_plot in self.__variance_values[network_type]):
                    cur_values = self.__values[network_type][what_to_plot]
                    cur_errors = self.__variance_values[network_type][what_to_plot]
                    
                    axes.errorbar(self.__timestamps[network_type][what_to_plot],
                                  cur_values, yerr=cur_errors,
                                  color=type(self).variance_colors[network_type])
            
            self.__ploteach(plotter, checks)


    def __plotTrace(self):
        if self.__network_trace_file and not self.__trace:
            self.__trace = IntNWBehaviorPlot.NetworkTrace(self.__network_trace_file,
                                                          self, self.__estimates, 
                                                          self.__is_server)
            transfers = self.__getAllUploads()
            self.__trace.addTransfers(transfers)
            
        checks = {'wifi': self.__show_wifi, '3G': self.__show_threeg}
        if self.__show_trace.isChecked():
            self.__trace.plot(self.__axes, self.__whatToPlot(), checks)
            if self.__show_trace_variance.isChecked():
                self.__trace.plotVariance(self.__axes, self.__whatToPlot(), checks)

    def __plotMeasurements(self):
        what_to_plot = self.__whatToPlot()
        if what_to_plot == "tx_time":
            self.__plotEstimatedTransferTimes()
        else:
            self.__plotMeasurementsSimple()
            

    class ActiveMeasurements(object):
        def __init__(self, infile):
            rx = re.compile("^\[([0-9]+\.[0-9]+)\] total .+ new .+ bandwidth ([0-9.]+) bytes/sec")
            self.__start = None
            self.__times = []
            self.__values = []
            for line in open(infile).readlines():
                m = rx.search(line)
                if m:
                    ts, bandwidth = [float(v) for v in m.groups()]
                    if self.__start is None:
                        self.__start = ts
                    self.__times.append(ts - self.__start)
                    self.__values.append(bandwidth)

        def plot(self, axes):
            axes.plot(self.__times, self.__values, label="active bandwidth measurements",
                      linestyle="-", linewidth=0.5)
        
    def __plotActiveMeasurements(self):
        measurements = \
            IntNWBehaviorPlot.ActiveMeasurements(self.__bandwidth_measurements_file)
        measurements.plot(self.__axes)

    def __plotEstimatedTransferTimes(self):
        checks = {'wifi': self.__show_wifi, '3G': self.__show_threeg}

        rel_times = {}
        network_errors = self.__error_history.getAllErrors()

        for network_type, metric in network_metric_pairs():
            init_dict(rel_times, network_type, {})
            init_dict(network_errors, network_type, {})
            init_dict(network_errors[network_type], metric, [])

            cur_times, observations, estimated_values = \
                self.__getAllEstimates(network_type, metric)
            rel_times[network_type][metric] = cur_times
            network_errors[network_type][metric].extend(
                get_error_values(observations, estimated_values))

        def get_errors(network_type, tx_start):
            errors = {}
            for metric in rel_times[network_type]:
                cur_times = rel_times[network_type][metric]
                cur_errors = network_errors[network_type][metric]
                
                pos = bisect_right(cur_times, tx_start)
                offset = len(cur_errors) - len(cur_times)
                assert offset >= 0
                errors[metric] = cur_errors[:offset+pos]

            return errors

        def get_estimates(network_type, tx_start):
            estimates = {}
            for metric in rel_times[network_type]:
                cur_times = rel_times[network_type][metric]
                cur_estimates = self.__getAllEstimates(network_type, metric)[2]
                
                pos = bisect_right(cur_times, tx_start)
                assert pos > 0
                estimates[metric] = cur_estimates[pos - 1]
            
            return estimates
            
        def transfer_time(bw, latency, size):
            return (size / bw) + latency

        transfers = self.__getAllUploads()
        
        predicted_transfer_durations = {'wifi': [], '3G': []}
        transfer_error_bounds = {'wifi': ([],[]), '3G': ([],[])}
        transfer_error_means = {'wifi': [], '3G': []}
        for network_type in ['wifi', '3G']:
            if not checks[network_type].isChecked():
                continue

            for tx_start, tx_size in transfers:
                cur_estimates = get_estimates(network_type, tx_start)
                est_tx_time = transfer_time(cur_estimates['bandwidth_up'],
                                            cur_estimates['latency'], tx_size)
                predicted_transfer_durations[network_type].append(est_tx_time)

                cur_errors = get_errors(network_type, tx_start)
                transfer_times_with_error = []
                for bandwidth_err, latency_err in product(cur_errors['bandwidth_up'], 
                                                          cur_errors['latency']):
                    bandwidth = error_adjusted_estimate(cur_estimates['bandwidth_up'],
                                                        bandwidth_err)
                    latency = error_adjusted_estimate(cur_estimates['latency'], 
                                                      latency_err)

                    if not RELATIVE_ERROR:
                        bandwidth = max(1.0, bandwidth)
                        latency = max(0.0, latency)
                    
                    tx_time = transfer_time(bandwidth, latency, tx_size)
                    if tx_time < 0.0 or tx_time > 500:
                        debug_trace()
                    transfer_times_with_error.append(tx_time)
                
                transfer_time_errors = [error_value(est_tx_time, tx_time) for tx_time in 
                                        transfer_times_with_error]
                transfer_time_error_means = stepwise_mean(transfer_time_errors)
                error_calculator = self.__getErrorCalculator()
                lower_errors, upper_errors = \
                    error_calculator([est_tx_time] * len(transfer_time_errors),
                                     transfer_time_errors, transfer_time_error_means)

                # use the last error as the cumulative error for transfer time
                transfer_error_bounds[network_type][0].append(lower_errors[-1])
                transfer_error_bounds[network_type][1].append(upper_errors[-1])
                transfer_error_means[network_type].append(transfer_time_error_means[-1])
        
            plotter = self.__getErrorPlotter()
            times = [tx_start for tx_start, tx_size in transfers]
            estimates_array = np.array(predicted_transfer_durations[network_type])
            error_adjusted_estimates = error_adjusted_estimate(
                estimates_array, np.array(transfer_error_means[network_type]))
            plotter(times, predicted_transfer_durations[network_type], 
                    error_adjusted_estimates,
                    transfer_error_bounds[network_type], network_type)
            
            self.__plotEstimates(times, predicted_transfer_durations[network_type], network_type)
            

    def __getAllUploads(self):
        all_irobs = {}
        
        def get_transfer(irob):
            return (self.getAdjustedTime(irob.getStart()), irob.getSize())
                
        for network_type in self.__networks:
            irobs = self.__networks[network_type]['up']
            for irob_id in irobs:
                new_irob = irobs[irob_id]
                if irob_id in all_irobs:
                    old_irob = all_irobs[irob_id]
                    all_irobs[irob_id] = min(old_irob, new_irob, 
                                             key=lambda x: x.getStart())
                else:
                    all_irobs[irob_id] = new_irob
                        
        irob_list = sorted(all_irobs.values(), key=lambda x: x.getStart())
        def within_bounds(irob):
            start = self.getAdjustedTime(irob.getStart())
            return start > 0.0
        transfers = [get_transfer(irob) for irob in irob_list if within_bounds(irob)]
        return transfers

    def __getAllEstimates(self, network_type, what_to_plot):
        estimates = self.__estimates[network_type][what_to_plot]
        times = [self.getAdjustedTime(e['timestamp']) for e in estimates]
        
        txform = 1.0
        if what_to_plot == 'latency':
            # RTT = latency * 2
            txform = 2.0
            
        observations = [e['observation'] * txform for e in estimates]
        estimated_values = [e['estimate'] * txform for e in estimates]

        return times, observations, estimated_values

    def __plotMeasurementsSimple(self):
        checks = {'wifi': self.__show_wifi, '3G': self.__show_threeg}
        what_to_plot = self.__whatToPlot()
        
        for network_type in self.__estimates:
            if not checks[network_type].isChecked():
                continue

            times, observations, estimated_values = \
                self.__getAllEstimates(network_type, what_to_plot)

            if self.__show_measurements.isChecked() and len(observations) > 0:
                error_history = \
                    self.__error_history.getErrors(network_type, what_to_plot)
                error_values = error_history + get_error_values(observations, estimated_values)
                
                error_means = stepwise_mean(error_values)

                error_calculator = self.__getErrorCalculator()
                plotter = self.__getErrorPlotter()

                error_bounds = error_calculator(estimated_values, error_values, error_means)
                estimates_array = np.array(estimated_values)
                error_means_array = np.array(error_means[-len(estimated_values):]) # tail of array
                error_adjusted_estimates = error_adjusted_estimate(
                    estimates_array, error_means_array)

                plotter(times, estimated_values, error_adjusted_estimates, 
                        error_bounds, network_type)
                
                self.__plotEstimates(times, estimated_values, network_type)
                self.__plotObservations(times, observations, network_type)

    def __getErrorCalculator(self):
        '''Return a function that calculates upper and lower error bounds
        based on three arrays: estimates, error_values, and error_means.
        
        estimates: value of estimator at different points in time
        error_values: samples of the estimator error, as measured by comparing
                      a new measured value to the previous estimate
                      (treating it as a prediction)
        error_means: the stepwise mean of the error values

        error_values and error_means must have the same length.
        if (M = len(estimates)) < (N = len(error_values)), the first (N-M) elements 
        of estimates are treated as *history* - not plotted, but used in the
        error interval calculations.'''
        if self.__error_error_ci.isChecked():
            return self.__getConfidenceInterval
        elif self.__error_error_stddev.isChecked():
            return self.__getErrorStddev
        else:
            assert False

    def __getErrorPlotter(self):
        if self.__plot_error_bars.isChecked():
            return self.__plotMeasurementErrorBars
        elif self.__plot_colored_error_regions.isChecked():
            return self.__plotColoredErrorRegions
        else:
            assert False

    def __getConfidenceInterval(self, estimated_values, error_values, error_means):
        error_variances = stepwise_variance(error_values)
        error_stddevs = [v ** 0.5 for v in error_variances]

        error_confidence_intervals = [0.0]
        error_confidence_intervals += \
            [confidence_interval(self.__alpha, stddev, max(n+1, 2)) 
             for n, stddev in enumerate(error_stddevs)][1:]

        return self.__error_range(estimated_values, error_means, 
                                  error_confidence_intervals, 
                                  error_confidence_intervals)

    def __error_range(self, estimates, error_means, 
                      lower_error_intervals, upper_error_intervals):
        num_estimates = len(estimates)
        estimates = np.array(estimates)

        # use the last num_estimates items in each of these arrays.
        # if they have the same size, we'll use all of them.
        # if len(error_means) > len(estimated_values), 
        #   we'll use the later error values, treating the rest as history.
        assert len(estimates) <= len(error_means)
        error_means = np.array(error_means[-num_estimates:])
        lower_error_intervals = np.array(lower_error_intervals[-num_estimates:])
        upper_error_intervals = np.array(upper_error_intervals[-num_estimates:])

        lower_errors = error_means - lower_error_intervals
        upper_errors = error_means + upper_error_intervals

        adjusted_estimates = error_adjusted_estimate(estimates, error_means)
        lowers = error_adjusted_estimate(estimates, lower_errors)
        uppers = error_adjusted_estimate(estimates, upper_errors)
        return lowers, uppers

    def __getErrorStddev(self, estimated_values, error_values, error_means):
        error_variances = stepwise_variance(error_values)
        error_stddevs = [v ** 0.5 for v in error_variances]

        return self.__error_range(estimated_values, error_means, 
                                  error_stddevs, error_stddevs)

    def __plotEstimates(self, times, estimated_values, network_type):
        self.__axes.plot(times, estimated_values,
                         label=network_type + " estimates",
                         color=self.__irob_colors[network_type])

    def __plotObservations(self, times, observations, network_type):
        markers = {'wifi': 's', '3G': 'o'}
        self.__axes.plot(times, observations, label=network_type + " observations",
                         linestyle='none', marker=markers[network_type],
                         markersize=3, color=self.__irob_colors[network_type])



    def __plotMeasurementErrorBars(self, times, estimated_values,
                                   error_adjusted_estimates, error_bounds, network_type):
        estimates = np.array(estimated_values)
        yerr = [estimates - error_bounds[0],  error_bounds[1] - estimates]
        
        self.__axes.errorbar(times, estimated_values, yerr=yerr,
                             color=self.__irob_colors[network_type])


    def __plotColoredErrorRegions(self, times, estimated_values, 
                                  error_adjusted_estimates, error_bounds, network_type):
        where = [True] * len(times)

        #if network_type == "wifi":
        if False: # this doesn't work too well yet, so I'm leaving it out until I fix it.
            wifi_periods = self.__getWifiPeriods()

            def get_mid(mid, left, right, left_val, right_val):
                slope = (right_val - left_val) / (right - left)
                delta = slope * (mid - left)
                return left_val + delta

            def split_region(times, values, split_time, new_value=None):
                pos = bisect_right(times, split_time)
                if new_value is not None:
                    mid = new_value
                elif pos == 0:
                    mid = values[0]
                elif pos == len(times):
                    mid = values[pos - 1]
                else:
                    mid = get_mid(split_time, times[pos - 1], times[pos],
                                  values[pos - 1], values[pos])
                    
                times.insert(pos, split_time)
                values.insert(pos, mid)
                return pos

            def insert_inflection_points_for_wifi(values):
                my_times = copy(times)
                my_where_times = copy(times)
                where = [True] * len(values)

                end_pos = None
                for start, length in wifi_periods:
                    split_region(my_times, values, start)
                    split_region(my_times, values, start + length)
                    start_pos = split_region(my_where_times, where, start, True)
                    if end_pos is not None:
                        for i in xrange(end_pos, start_pos):
                            # mark slices between last wifi end and current wifi start
                            #  as no-wifi
                            where[i] = False

                    end_pos = split_region(my_where_times, where, start + length, False)
                    for i in xrange(start_pos, end_pos+1):
                        # mark slices during this wifi period to be plotted
                        where[i] = True

                return my_times, where

            new_values = []
            value_lists = [list(v) for v in [estimated_values,
                                             error_adjusted_estimates, 
                                             error_bounds[0],
                                             error_bounds[1]]]
            for value_list in value_lists:
                new_times, new_where = insert_inflection_points_for_wifi(value_list)
                new_values.append(value_list)

            estimated_values, error_adjusted_estimates = new_values[:2]
            error_bounds = new_values[2:]
            times = new_times
            where = new_where

        where = np.array(where)

        for the_where, alpha in [(where, 0.5), (~where, 0.1)]:
            self.__axes.fill_between(times, error_bounds[1], error_bounds[0], where=the_where,
                                     facecolor=self.__irob_colors[network_type],
                                     alpha=alpha)

        self.__axes.plot(times, error_adjusted_estimates,
                         color=self.__irob_colors[network_type],
                         linestyle='--', linewidth=0.5)
                                 
    def __setupAxes(self):
        yticks = {}
        for network, pos in self.__network_pos_offsets.items():
            for direction, offset in self.__direction_pos_offsets.items():
                label = "%s %s" % (network, direction)
                yticks[pos + offset] = label

        min_tick = min(yticks.keys())
        max_tick = max(yticks.keys())
        self.__axes.set_ylim(min_tick - self.__irob_height,
                             max_tick + self.__irob_height)
        self.__axes.set_yticks(yticks.keys())
        self.__axes.set_yticklabels(yticks.values())

    def __setTraceEnd(self):
        for network_type in self.__network_periods:
            periods = self.__network_periods[network_type]
            if periods:
                periods[-1]['end'] = self.__end

    def __drawIROBs(self):
        for network_type in self.__networks:
            network = self.__networks[network_type]
            for direction in network:
                irobs = network[direction]
                for irob_id in irobs:
                    irob = irobs[irob_id]
                    irob.draw(self.__axes)

    def __getSessionAxes(self, reset=False):
        if self.__session_axes == None or reset:
            self.__session_axes = self.__axes.twinx()
        return self.__session_axes

    def __drawSessions(self, **kwargs):
        if self.__sessions and self.__show_sessions.isChecked():
            self.__drawSomeSessions(self.__sessions, **kwargs)

        if self.__choose_network_calls:
            timestamps, calls = zip(*self.__choose_network_calls)
            timestamps = [self.getAdjustedTime(t) for t in timestamps]
            self.__getSessionAxes().plot(timestamps, calls, label="choose_network")

    def __drawSomeSessions(self, sessions, **kwargs):
        timestamps = [self.getAdjustedTime(s['start']) for s in sessions]
        session_times = [s['end'] - s['start'] for s in sessions]
        
        if 'marker' not in kwargs:
            kwargs['marker'] = 'o'
        if 'markersize' not in kwargs:
            kwargs['markersize'] = 3
        if 'color' not in kwargs:
            kwargs['color'] = 'black'
            
        ax = self.__getSessionAxes()
        ax.plot(timestamps, session_times, **kwargs)

    def __drawRedundancyDecisions(self):
        if self.__redundancy_decisions and self.__show_decisions.isChecked():
            timestamps = [self.getAdjustedTime(d.timestamp)
                          for d in self.__redundancy_decisions]
            redundancy_benefits = [d.benefit for d in self.__redundancy_decisions]
            redundancy_costs = [d.cost for d in self.__redundancy_decisions]

            ax = self.__getSessionAxes()
            ax.plot(timestamps, redundancy_benefits, marker='o',
                    markersize=3, color='green')
            ax.plot(timestamps, redundancy_costs, marker='o', markersize=3, color='orange')

    def __getWifiPeriods(self):
        wifi_periods = filter(lambda p: p['end'] is not None,
                              self.__network_periods['wifi'])
        return [(self.getAdjustedTime(period['start']),
                 period['end'] - period['start'])
                for period in wifi_periods]

    def __drawWifi(self):
        if "wifi" not in self.__network_periods:
            # not done parsing yet
            return

        bars = self.__getWifiPeriods()
        vertical_bounds = self.__axes.get_ylim()
        height = [vertical_bounds[0] - self.__irob_height / 2.0,
                  vertical_bounds[1] - vertical_bounds[0] + self.__irob_height]
        self.__axes.broken_barh(bars, height, color="green", alpha=0.2)

    def printStats(self):
        if self.__choose_network_calls:
            choose_network_times = [c[1] for c in self.__choose_network_calls]
            print ("%f seconds in chooseNetwork (%d calls)" %
                   (sum(choose_network_times), len(choose_network_times)))

        self.__printRedundancyBenefitAnalysis()
        self.__printIROBTimesByNetwork()

    def __printRedundancyBenefitAnalysis(self):
        if "wifi" not in self.__network_periods:
            # not done parsing yet
            return

        wifi_periods = self.__getWifiPeriods()
        num_sessions = len(self.__sessions)

        def one_network_only(session):
            session_start = self.getAdjustedTime(session['start'])
            for start, length in wifi_periods:
                if session_start >= start and session_start <= (start + length):
                    return False
            return True

        def duration(session):
            return session['end'] - session['start']

        # XXX: could do this more efficiently by marking it
        # XXX: when we first read the log.
        single_network_sessions = filter(one_network_only, self.__sessions)
        print ("Single network sessions: %d/%d (%.2f%%), total time %f seconds" %
               (len(single_network_sessions), num_sessions,
                len(single_network_sessions)/float(num_sessions) * 100,
                sum([duration(s) for s in single_network_sessions])))


        def failed_over(session):
            session_start = self.getAdjustedTime(session['start'])
            session_end = session_start + duration(session)

            if one_network_only(session):
                return False
            
            for start, length in wifi_periods:
                if (session_start >= start and session_start <= (start + length) and
                    session_end > (start + length)):
                        return True

            matching_irobs = self.__getIROBs(session_start, session_end)
            # irobs['wifi']['down'] => [IROB(),...]
            
            # get just the irobs without the dictionary keys
            irobs = sum([v.values() for v in matching_irobs.values()], [])
            # list of lists of IROBs
            irobs = sum(irobs, [])
            # list of IROBs


            dropped_irobs = filter(lambda x: x.wasDropped(), irobs)
            fail = (len(dropped_irobs) > 0)
            if fail:
                dprint("Session: %s" % session)
                dprint("  IROBs: " % irobs)
            return fail

        failover_sessions = filter(failed_over, self.__sessions)
        print ("Failover sessions: %d/%d (%.2f%%), total %f seconds" %
               (len(failover_sessions), num_sessions,
                len(failover_sessions)/float(num_sessions) * 100,
                sum([duration(s) for s in failover_sessions])))
        
        # check the sessions that started in a single-network period
        # but finished after wifi arrived.
        def needed_reevaluation(session):
            session_start = self.getAdjustedTime(session['start'])
            session_end = session_start + duration(session)
            for start, length in wifi_periods:
                if session_start >= start and session_start <= (start + length):
                    return False
                # didn't start during this wifi period.
                # did this wifi period come in the middle of the session?
                if start > session_start and start < session_end:
                    # wifi arrived sometime during session
                    return True
            return False

        reevaluation_sessions = filter(needed_reevaluation, self.__sessions)
        print ("Needed-reevaluation sessions: %d/%d (%.2f%%)" %
               (len(reevaluation_sessions), num_sessions,
                len(reevaluation_sessions)/float(num_sessions) * 100))

        # TODO: print average wifi, 3G session times

        self.__debug_sessions = failover_sessions
        #self.__debug_sessions = reevaluation_sessions

    def __printIROBTimesByNetwork(self):
        irobs = self.__getIROBs()
        print "Average IROB durations:"
        for network_type, direction in product(['wifi', '3G'], ['down', 'up']):
            dprint("%s sessions:" % network_type)
            times = [irob.getDuration() for irob in irobs[network_type][direction]]

            if len(times) > 0:
                avg = sum(times) / len(times)
                print "  %5s, %4s: %f" % (network_type, direction, avg)
            else:
                print "  %5s, %4s: (no IROBs)" % (network_type, direction)


    def __getIROBs(self, start=-1.0, end=None):
        """Get all IROBs that start in the specified time range.

        Returns a dictionary: d[network_type][direction] => [IROB(),...]

        start -- relative starting time
        end -- relative ending time

        """
        if end is None:
            end = self.__end + 1.0

        matching_irobs = {'wifi': {}, '3G': {}}
        def time_matches(irob):
            irob_start, irob_end = [self.getAdjustedTime(t) for t in irob.getTimeInterval()]
            return (irob_start >= start and irob_end <= end)
            
        for network_type, direction in product(['wifi', '3G'], ['down', 'up']):
            irobs = self.__networks[network_type][direction].values()
            matching_irobs[network_type][direction] = filter(time_matches, irobs)
        return matching_irobs

    def __drawDebugging(self):
        self.__drawSomeSessions(self.__debug_sessions,
                                marker='s', color='red', markersize=10,
                                markerfacecolor='none', linestyle='none')


    def getIROBPosition(self, irob):
        # TODO: allow for simultaneous (stacked) IROB plotting.
        
        return (self.__network_pos_offsets[irob.network_type] +
                self.__direction_pos_offsets[irob.direction])
        
    def getIROBHeight(self, irob):
        # TODO: adjust based on the number of stacked IROBs.
        return self.__irob_height

    def getIROBColor(self, irob):
        return self.__irob_colors[irob.network_type]

    def getAdjustedTime(self, timestamp):
        return timestamp - self.__start

    def getAdjustedTraceLatency(self, latency):
        if not self.__cross_country_latency or latency < 0.0001:
            return latency
        
        LATENCY_ADJUSTMENT = 0.100 # 100ms cross-country
        return latency + LATENCY_ADJUSTMENT

    def setStart(self, start):
        # for resetting the experiment start, to avoid including the
        #  setup transfers and waiting time at the server.
        self.__start = start

    def parseLine(self, line):
        timestamp = getTimestamp(line)
        if self.__start == None:
            self.__start = timestamp
            
        self.__end = timestamp

        if "Got update from scout" in line:
            #[time][pid][tid] Got update from scout: 192.168.1.2 is up,
            #                 bandwidth_down 43226 bandwidth_up 12739 bytes/sec RTT 97 ms
            #                 type wifi
            ip, status, network_type = re.search(self.__network_regex, line).groups()
            if not self.__is_server:
                self.__modifyNetwork(timestamp, ip, status, network_type)
        elif "Successfully bound" in line:
            # [time][pid][CSockSender 57] Successfully bound osfd 57 to 192.168.1.2:0
            self.__addConnection(line)
        elif "Adding connection" in line:
            # [time][pid][Listener 13] Adding connection 14 from 192.168.1.2
            #                          bw_down 43226 bw_up 12739 RTT 97
            #                          type wifi(peername 141.212.110.115)
            self.__addIncomingConnection(line, timestamp)
        elif re.search(self.__csocket_destroyed_regex, line) != None:
            # [time][pid][CSockSender 57] CSocket 57 is being destroyed
            self.__removeConnection(line)
        elif "Getting bytes to send from IROB" in line:
            # [time][pid][CSockSender 57] Getting bytes to send from IROB 6
            irob = int(line.strip().split()[-1])
            network = self.__getNetworkType(line)
            
            self.__currentSendingIROB = irob
            self.__addIROB(timestamp, network, irob, 'up')
        elif "...returning " in line:
            # [time][pid][CSockSender 57] ...returning 1216 bytes, seqno 0
            assert self.__currentSendingIROB != None
            datalen = int(line.strip().split()[3])
            network = self.__getNetworkType(line)
            self.__addIROBBytes(timestamp, network, self.__currentSendingIROB,
                                datalen, 'up')
        elif "About to send message" in line:
            # [time][pid][CSockSender 57] About to send message:  Type: Begin_IROB(1)
            #                             Send labels: FG,SMALL IROB: 0 numdeps: 0
            self.__addTransfer(line, 'up')
        elif "Received message" in line:
            # [time][pid][CSockReceiver 57] Received message:  Type: Begin_IROB(1)
            #                               Send labels: FG,SMALL IROB: 0 numdeps: 0
            self.__addTransfer(line, 'down')
        elif "network estimator" in line:
            network_type = re.search(self.__network_estimator_regex, line).group(1)
            dprint("got observation: %s" % line)
            bw_match = re.search(self.__network_bandwidth_regex, line)
            lat_match = re.search(self.__network_latency_regex, line)
            bw, latency = None, None
            if bw_match and float(bw_match.groups()[0]) > 0.0:
                bw = bw_match.groups()
            if lat_match and float(lat_match.groups()[0]) > 0.0:
                latency = lat_match.groups()
            
            self.__addEstimates(network_type, timestamp, bw=bw, latency=latency)
        elif "New spot values" in line:
            # TODO: parse values, call self.__addNetworkObservation
            network_type = self.__getNetworkType(line)
            pass
        elif "New estimates" in line:
            # TODO: parse values, call self.__addNetworkEstimate
            network_type = self.__getNetworkType(line)
            pass
        elif "chooseNetwork" in line:
            duration = timestamp - getTimestamp(self.__last_line)
            time_match = re.search(choose_network_time_regex, line)
            if time_match:
                duration = float(time_match.group(1))
            self.__choose_network_calls.append((timestamp, duration))
        elif "redundancy_strategy_type" in line:
            # [timestamp][pid][Bootstrapper 49] Sending hello:  Type: Hello(0)
            #                                   Send labels:  listen port: 42424
            #                                   num_ifaces: 2 
            #                                   redundancy_strategy_type: intnw_redundant
            redundancy_strategy = \
                re.search(self.__redundancy_strategy_regex, line).group(1)
            if redundancy_strategy not in self.__title:
                self.__title += " - " + redundancy_strategy
        else:
            pass # ignore it
            
        self.__last_line = line

    def __initRegexps(self):
        self.__irob_regex = re.compile("IROB: ([0-9]+)")
        self.__datalen_regex = re.compile("datalen: ([0-9]+)")
        self.__expected_bytes_regex = re.compile("expected_bytes: ([0-9]+)")
        self.__network_regex = re.compile("scout: (.+) is (down|up).+ type ([A-Za-z0-9]+)")

        ip_regex_string = "([0-9]+(?:\.[0-9]+){3})"
        self.__ip_regex = re.compile(ip_regex_string)
        
        self.__socket_regex = re.compile("\[CSock(?:Sender|Receiver) ([0-9]+)\]")
        self.__intnw_message_type_regex = \
            re.compile("(?:About to send|Received) message:  Type: ([A-Za-z_]+)")
        self.__csocket_destroyed_regex = re.compile("CSocket ([0-9]+) is being destroyed")
        self.__network_estimator_regex = \
            re.compile("Adding new stats to (.+) network estimator")

        float_regex = "([0-9]+" + "(?:\.[0-9]+)?)"
        stats_regex = "obs %s est %s" % (float_regex, float_regex)
        self.__network_bandwidth_regex = re.compile("bandwidth: " + stats_regex)
        self.__network_latency_regex = re.compile("latency: " + stats_regex)

        self.__redundancy_strategy_regex = \
            re.compile("redundancy_strategy_type: ([a-z_]+)\s*")

        self.__incoming_connection_regex = \
            re.compile("Adding connection ([0-9]+) from " + ip_regex_string +
                       ".+type ([A-Za-z0-9]+)")
        
    def __getIROBId(self, line):
        return int(re.search(self.__irob_regex, line).group(1))

    def __getSocket(self, line):
        return int(re.search(self.__socket_regex, line).group(1))

    def __getIP(self, line):
        return re.search(self.__ip_regex, line).group(1)

    def __addNetworkType(self, network_type):
        if network_type not in self.__networks:
            self.__networks[network_type] = {
                'down': {}, # download IROBs
                'up': {}    # upload IROBs
                }
            self.__network_periods[network_type] = []

    def __modifyNetwork(self, timestamp, ip, status, network_type):
        self.__addNetworkType(network_type)
        
        if status == 'down':
            period = self.__network_periods[network_type][-1]
            if period['end'] is not None:
                print "Warning: double-ending %s period at %f" % (network_type, timestamp)
            period['end'] = timestamp
            
            if ip in self.__network_type_by_ip:
                del self.__network_type_by_ip[ip]
        elif status == 'up':
            self.__startNetworkPeriod(network_type, ip,
                                      start=timestamp, end=None, sock=None)
            
            placeholder = (ip in self.__network_type_by_ip and
                           self.__network_type_by_ip[ip] == "placeholder")
            self.__network_type_by_ip[ip] = network_type
            if placeholder:
                sock = self.__placeholder_sockets[ip]
                del self.__placeholder_sockets[ip]
                self.__addNetworkPeriodSocket(sock, ip)
        else: assert False

    def __startNetworkPeriod(self, network_type, ip, start, end=None, sock=None):
        periods = self.__network_periods[network_type]
        if len(periods) > 0 and periods[-1]['end'] == None:
            # two perfectly adjacent periods with no 'down' in between.  whatevs.
            periods[-1]['end'] = start

        periods.append({
            'start': start, 'end': end,
            'ip': ip, 'sock': sock
            })
        
    def __addConnection(self, line):
        sock = self.__getSocket(line)
        ip = self.__getIP(line)
        if ip not in self.__network_type_by_ip:
            self.__network_type_by_ip[ip] = "placeholder"
            assert ip not in self.__placeholder_sockets
            self.__placeholder_sockets[ip] = sock
        else:
            self.__addNetworkPeriodSocket(sock, ip)

    def __addEstimates(self, network_type, timestamp, bw=None, latency=None):
        if network_type not in self.__estimates:
            self.__estimates[network_type] = {}
                
        for values, name in zip((bw, latency), ("bandwidth_up", "latency")):
            if values:
                obs, est = values
                self.__addNetworkObservation(network_type, name,
                                             float(timestamp), float(obs))
                self.__addNetworkEstimate(network_type, name, float(est))

    def __getEstimates(self, network_type, name):
        all_estimates = self.__estimates[network_type]
        if name not in all_estimates:
            all_estimates[name] = []
        return all_estimates[name]
                
    def __addNetworkObservation(self, network_type, name, timestamp, obs):
        if obs == 0.0:
            debug_trace()
            
        estimates = self.__getEstimates(network_type, name)
        estimates.append({'timestamp': float(timestamp),
                          'observation': float(obs),
                          'estimate': None})

    def __addNetworkEstimate(self, network_type, name, est):
        estimates = self.__getEstimates(network_type, name)
        assert estimates[-1]['estimate'] is None
        estimates[-1]['estimate'] = est

    def __addNetworkPeriodSocket(self, sock, ip):
        network_type = self.__network_type_by_ip[ip]
        network_period = self.__network_periods[network_type][-1]

        assert network_period['start'] != None
        assert network_period['ip'] == ip
        assert network_period['sock'] == None
        network_period['sock'] = sock

        assert sock not in self.__network_type_by_sock
        self.__network_type_by_sock[sock] = network_type

    def __addIncomingConnection(self, line, timestamp):
        match = re.search(self.__incoming_connection_regex, line)
        sock, ip, network_type = match.groups()
        sock = int(sock)

        self.__addNetworkType(network_type)
        self.__startNetworkPeriod(network_type, ip, start=timestamp, end=None, sock=None)
        
        self.__network_type_by_ip[ip] = network_type
        self.__addNetworkPeriodSocket(sock, ip)
        
    def __removeConnection(self, line):
        timestamp = getTimestamp(line)
        sock = int(re.search(self.__csocket_destroyed_regex, line).group(1))
        if sock in self.__network_type_by_sock:
            network_type = self.__network_type_by_sock[sock]
            network_period = self.__network_periods[network_type][-1]
            
            network_period['sock'] = None
            del self.__network_type_by_sock[sock]

            self.__markDroppedIROBs(timestamp, network_type)

            if self.__is_server:
                # client will get update from scout; server won't
                self.__modifyNetwork(timestamp, network_period['ip'], 'down', network_type)

    def __getNetworkType(self, line):
        sock = self.__getSocket(line)
        return self.__network_type_by_sock[sock]

    def __getDatalen(self, line):
        return int(re.search(self.__datalen_regex, line).group(1))

    def __getExpectedBytes(self, line):
        return int(re.search(self.__expected_bytes_regex, line).group(1))

    def __getIntNWMessageType(self, line):
        return re.search(self.__intnw_message_type_regex, line).group(1)

    def __addTransfer(self, line, direction):
        # [time][pid][CSockSender 57] About to send message:  Type: Begin_IROB(1)
        #                             Send labels: FG,SMALL IROB: 0 numdeps: 0
        # [time][pid][CSockReceiver 57] Received message:  Type: IROB_chunk(3)
        #                               Send labels: FG,SMALL IROB: 0
        #                               seqno: 0 offset: 0 datalen: 1024
        # [time][pid][CSockReceiver 57] Received message:  Type: End_IROB(2)
        #                               Send labels: FG,SMALL IROB: 0
        #                               expected_bytes: 1024 expected_chunks: 1
        # [time][pid][CSockReceiver 57] Received message:  Type: Ack(7)
        #                               Send labels:  num_acks: 0 IROB: 0
        #                               srv_time: 0.000997 qdelay: 0.000000
        timestamp = getTimestamp(line)
        network_type = self.__getNetworkType(line)
        intnw_message_type = self.__getIntNWMessageType(line)

        if (intnw_message_type == "Begin_IROB" or
            intnw_message_type == "Data_Check"):
            irob_id = self.__getIROBId(line)
            if direction == "up":
                self.__currentSendingIROB = None
            self.__addIROB(timestamp, network_type, irob_id, direction)
        elif intnw_message_type == "IROB_chunk":
            irob_id = self.__getIROBId(line)
            datalen = self.__getDatalen(line)
            self.__addIROBBytes(timestamp, network_type, irob_id, datalen, direction)
        elif intnw_message_type == "End_IROB" and direction == 'down':
            irob_id = self.__getIROBId(line)
            expected_bytes = self.__getExpectedBytes(line)
            self.__finishReceivedIROB(timestamp, network_type, irob_id, expected_bytes)
        elif intnw_message_type == "Ack" and direction == 'down':
            irob_id = self.__getIROBId(line)
            self.__ackIROB(timestamp, network_type, irob_id, 'up')
        else:
            pass # ignore other types of messages

    def __getIROB(self, network_type, irob_id, direction, start=None):
        if network_type not in self.__networks:
            raise LogParsingError("saw data on unknown network '%s'" % network_type)
        irobs = self.__networks[network_type][direction]
        if irob_id not in irobs:
            if start != None:
                irobs[irob_id] = IROB(self, network_type, direction, start, irob_id)
            else:
                return None
            
        return irobs[irob_id]

    def __getIROBOrThrow(self, network_type, irob_id, direction, start=None):
        irob = self.__getIROB(network_type, irob_id, direction, start)
        if irob == None:
            raise LogParsingError("Unknown IROB %d" % irob_id)
        return irob
    
    def __addIROB(self, timestamp, network_type, irob_id, direction):
        # TODO: deal with the case where the IROB announcement arrives after the data
        irob = self.__getIROBOrThrow(network_type, irob_id, direction, start=timestamp)
        dprint("Adding %s at %f" % (irob, timestamp))


    def __addIROBBytes(self, timestamp, network_type, irob_id, datalen, direction):
        # XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
        # XXX: this is double-counting when chunk messages do appear.  fix it.
        # XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
        irob = self.__getIROBOrThrow(network_type, irob_id, direction)
        irob.addBytes(timestamp, datalen)

    def __finishReceivedIROB(self, timestamp, network_type, irob_id, expected_bytes):
        irob = self.__getIROBOrThrow(network_type, irob_id, 'down')
        irob.finish(timestamp, expected_bytes)

        # "finish" the IROB by marking it ACK'd.
        # Strictly speaking, a received IROB isn't "finished" until
        #  all expected bytes are received, but until the end_irob messasge
        #  arrives, we don't know how many to expect, so we hold off on
        #  marking it finished until then.
        dprint("Finished %s at %f" % (irob, timestamp))
        irob.ack(timestamp)

    def __markDroppedIROBs(self, timestamp, network_type):
        for direction in ['down', 'up']:
            for irob in self.__networks[network_type][direction].values():
                if not irob.complete():
                    irob.markDropped(timestamp)

    def __ackIROB(self, timestamp, network_type, irob_id, direction):
        irob = self.__getIROBOrThrow(network_type, irob_id, direction)
        irob.ack(timestamp)
        dprint("Acked %s at %f" % (irob, timestamp))

class RedundancyDecision(object):
    def __init__(self, timestamp):
        self.timestamp = timestamp;
        self.benefit = None
        self.cost = None

class ErrorHistory(object):
    def __init__(self):
        self.__history = {}

    def read(self, filename):
        with open(filename) as f:
            num_estimators = int(f.readline().split()[0])
            for i in xrange(num_estimators):
                fields = f.readline().split()
                network_type, what_to_plot = fields[0].split("-")
                error_distribution_type = fields[1] #TODO: use this?
                num_samples = int(fields[2])

                network_type = network_type.replace("cellular", "3G")
                what_to_plot = what_to_plot.replace("bandwidth", "bandwidth_up")
                what_to_plot = what_to_plot.replace("RTT", "latency")
                if network_type not in self.__history:
                    self.__history[network_type] = {}
                if what_to_plot not in self.__history[network_type]:
                    self.__history[network_type][what_to_plot] = []
                    
                errors = self.__history[network_type][what_to_plot]
                while len(errors) < num_samples:
                    line = f.readline().strip()
                    new_errors = [float(x) for x in line.split()]
                    errors.extend(new_errors)
        
    def getErrors(self, network_type, what_to_plot):
        if network_type in self.__history and what_to_plot in self.__history[network_type]:
            return self.__history[network_type][what_to_plot]
        
        return []

    def getAllErrors(self):
        return dict(self.__history)

class IntNWPlotter(object):
    def __init__(self, args):
        self.__windows = []
        self.__currentPid = None
        self.__pid_regex = re.compile("^\[[0-9]+\.[0-9]+\]\[([0-9]+)\]")
        self.__session_regex = re.compile("start ([0-9]+\.[0-9]+)" + 
                                          ".+duration ([0-9]+\.[0-9]+)")

        intnw_log = "intnw.log"
        trace_replayer_log = "trace_replayer.log"
        instruments_log = "instruments.log"
        self.__is_server = args.server
        if args.server:
            intnw_log = trace_replayer_log = instruments_log = "replayer_server.log"

        self.__measurements_only = args.measurements
        self.__network_trace_file = args.network_trace_file
        self.__bandwidth_measurements_file = args.bandwidth_measurements_file
        self.__intnw_log = args.basedir + "/" + intnw_log
        self.__trace_replayer_log = args.basedir + "/" + trace_replayer_log
        self.__redundancy_eval_log = args.basedir + "/" + instruments_log
        self.__cross_country_latency = args.cross_country_latency
        self.__history_dir = args.history
        
        self.__readFile(self.__intnw_log)
        self.draw()
        self.printStats()

    def draw(self):
        for window in self.__windows:
            window.on_draw()
            window.saveErrorTable()

    def printStats(self):
        for window in self.__windows:
            window.printStats()
        
    def __getPid(self, line):
        match = re.search(self.__pid_regex, line)
        if match:
            return int(match.group(1))

        return None

    def __getSession(self, line):
        match = re.search(self.__session_regex, line)
        if not match:
            raise LogParsingError("expected timestamp and duration")

        return [float(s) for s in match.groups()]

    def __readSessions(self):
        filename = self.__trace_replayer_log
        runs = []
        new_run = True
        for linenum, line in enumerate(open(filename).readlines()):
            fields = line.strip().split()
            if "Session times:" in line:
                # start over with the better list of sessions
                if len(runs[-1]) == 0:
                    runs = runs[:-1]
                runs[-1] = []
            elif "  Session" in line:
                timestamp, duration = self.__getSession(line)
                transfer = {'start': timestamp, 'end': timestamp + duration}

                sessions = runs[-1]
                sessions.append(transfer)
            else:
                try:
                    timestamp = float(fields[0])
                except (ValueError, IndexError):
                    continue

            if "Executing:" in line and fields[2] == "at":
                # start of a session
                transfer = {'start': timestamp, 'end': None}
                runs[-1].append(transfer)
            elif (("Waiting to execute" in line and fields[4] == "at") or
                  "Waiting until trace end" in line):
                if new_run:
                    # new run
                    runs.append([])
                    new_run = False
                elif len(runs[-1]) > 0:
                    # end of a session
                    runs[-1][-1]['end'] = timestamp
            elif "Done with trace replay" in line:
                new_run = True

        if len(runs[-1]) == 0:
            runs = runs[:-1]
        return runs

    def __lineStartsNewRun(self, line, current_pid):
        if self.__is_server:
            return ("Accepting connection from" in line)
        else:
            pid = self.__getPid(line)
            return pid != current_pid

    def __readRedundancyDecisions(self):
        filename = self.__redundancy_eval_log
        if not os.path.exists(filename):
            return
        
        benefit_regex = re.compile("Redundant strategy benefit: ([0-9.-]+)")
        cost_regex = re.compile("Redundant strategy additional cost: ([0-9.-]+)")
        
        runs = []
        last_pid = 0
        for linenum, line in enumerate(open(filename).readlines()):
            pid = self.__getPid(line)
            if self.__lineStartsNewRun(line, last_pid):
                runs.append([])
                
            last_pid = pid

            benefit_match = re.search(benefit_regex, line)
            cost_match = re.search(cost_regex, line)
            if benefit_match or cost_match:
                current_run = runs[-1]

            if benefit_match:
                timestamp = getTimestamp(line)
                decision = RedundancyDecision(timestamp)
                decision.benefit = float(benefit_match.group(1))
                dprint("Redundancy benefit: %f" % decision.benefit)
                
                current_run.append(decision)
            elif cost_match:
                decision = current_run[-1]
                decision.cost = float(cost_match.group(1))
                dprint("Redundancy cost: %f" % decision.cost)
        return runs

    def __readFile(self, filename):
        session_runs = None
        redundancy_decisions = None
        if self.__trace_replayer_log:
            session_runs = self.__readSessions()
        if self.__redundancy_eval_log:
            redundancy_decisions_runs = self.__readRedundancyDecisions()
        
        error_history = ErrorHistory()
        if self.__history_dir:
            side = "server" if self.__is_server else "client"
            history_filename = "%s/%s_error_distributions.txt" % (self.__history_dir, side)
            
            error_history.read(history_filename)
        
        print "Parsing log file..."
        progress = ProgressBar()
        for linenum, line in enumerate(progress(open(filename).readlines())):
            try:
                pid = self.__getPid(line)
                if pid == None:
                    continue

                if self.__lineStartsNewRun(line, self.__currentPid):
                    start = session_runs[len(self.__windows)][0]['start']
                    window = IntNWBehaviorPlot(len(self.__windows) + 1, start, 
                                               self.__measurements_only,
                                               self.__network_trace_file,
                                               self.__bandwidth_measurements_file,
                                               self.__cross_country_latency,
                                               error_history, self.__is_server)
                    self.__windows.append(window)
                    window_num = len(self.__windows) - 1
                    if session_runs:
                        sessions = session_runs[window_num]
                        self.__windows[-1].setSessions(sessions)
                    if redundancy_decisions_runs:
                        redundancy_decisions = redundancy_decisions_runs[window_num]
                        self.__windows[-1].setRedundancyDecisions(redundancy_decisions)

                self.__currentPid = pid
                
                if len(self.__windows) > 0:
                    self.__windows[-1].parseLine(line)
            except LogParsingError as e:
                trace = sys.exc_info()[2]
                e.setLine(linenum + 1, line)
                raise e, None, trace
            except Exception as e:
                trace = sys.exc_info()[2]
                e = LogParsingError(repr(e) + ": " + str(e))
                e.setLine(linenum + 1, line)
                raise e, None, trace

    def show(self):
        for window in self.__windows:
            window.show()

           
def exception_hook(type, value, tb):
    from PyQt4.QtCore import pyqtRemoveInputHook 
    import traceback, pdb
    traceback.print_exception(type, value, tb)
    pyqtRemoveInputHook()
    pdb.pm()

def debug_trace():
    from PyQt4.QtCore import pyqtRemoveInputHook 
    import pdb
    pyqtRemoveInputHook()
    pdb.set_trace()

import sys
sys.excepthook = exception_hook
    
def main():
    parser = ArgumentParser()
    parser.add_argument("basedir")
    parser.add_argument("--measurements", action="store_true", default=False)
    parser.add_argument("--network-trace-file", default=None)
    parser.add_argument("--bandwidth-measurements-file", default=None)
    parser.add_argument("--noplot", action="store_true", default=False)
    parser.add_argument("--cross-country-latency", action="store_true", default=False,
                        help="Add 100ms latency when plotting the trace.")
    parser.add_argument("--server", action="store_true", default=False,
                        help="Look in replayer_server.log for all logging.")
    parser.add_argument("--absolute-error", action="store_true", default=False,
                        help="Use absolute error rather than relative error in calculations")
    parser.add_argument("--history", default=None,
                        help=("Start the plotting with error history from files in this directory:"
                              +" {client,server}_error_distributions.txt"))
    parser.add_argument("-d", "--debug", action="store_true", default=False)
    args = parser.parse_args()

    global debug
    debug = args.debug

    app = QApplication(sys.argv)

    if args.absolute_error:
        global RELATIVE_ERROR
        RELATIVE_ERROR = False
    
    plotter = IntNWPlotter(args)
    if not args.noplot:
        plotter.show()
        app.exec_()

if __name__ == '__main__':
    main()
