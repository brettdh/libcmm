#!/usr/bin/env python2.7

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

import sys, re, os
from argparse import ArgumentParser

from PyQt4.QtCore import *
from PyQt4.QtGui import *

import matplotlib
from matplotlib.backends.backend_qt4agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.backends.backend_qt4agg import NavigationToolbar2QTAgg as NavigationToolbar
from matplotlib.figure import Figure

from itertools import product

from progressbar import ProgressBar

sys.path.append("../../scripts/nistnet_scripts/traces")
import mobility_trace

debug = False
#debug = True
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
        if x == 0.0:
            # ignore observations where wifi isn't present
            variances.append(0.0)
            continue
        n += 1
        delta = x - mean
        mean += delta / n
        M2 += delta * (x - mean)
        
        if n > 1:
            variances.append(M2 / (n - 1))
            
    assert len(variances) == len(data)
    return variances

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

    def getDuration(self):
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

    def __str__(self):
        return ("{IROB: id %d  direction: %s  network: %s}"
                % (self.irob_id, self.direction, self.network_type))

    def draw(self, axes):
        ypos = self.__plot.getIROBPosition(self)
        yheight = self.__plot.getIROBHeight(self)
        start, finish = [self.__plot.getAdjustedTime(ts) for ts in self.getDuration()]
        dprint("%s %f--%f" % (self, start, finish))
        axes.broken_barh([[start, finish-start]],
                         [ypos - yheight / 2.0, yheight],
                         color=self.__plot.getIROBColor(self))

        if self.__drop_time != None:
            axes.plot([finish], [ypos], marker='x', color='black', markeredgewidth=2.0)
        elif self.__abnormal_end:
            axes.plot([finish], [ypos], marker='*', color='black')

timestamp_regex = re.compile("^\[([0-9]+\.[0-9]+)\]")

def getTimestamp(line):
    return float(re.search(timestamp_regex, line).group(1))
            

class IntNWBehaviorPlot(QDialog):
    def __init__(self, run, measurements_only, network_trace_file, parent=None):
        QDialog.__init__(self, parent)

        self.__initRegexps()
        self.__run = run
        self.__networks = {}
        self.__network_periods = {}
        self.__network_type_by_ip = {}
        self.__network_type_by_sock = {}
        self.__placeholder_sockets = {} # for when connection begins before scout msg

        self.__measurements_only = measurements_only
        self.__network_trace_file = network_trace_file
        self.__trace = None

        # estimates[network_type][bandwidth_up|latency] -> [values]
        self.__estimates = {'wifi': {}, '3G': {}}

        # second axes to plot times on
        self.__session_axes = None
        self.__user_set_max_time = None

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

        self.__start = None

        # TODO: infer plot title from file path
        self.__title = "IntNW - Run %d" % self.__run

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
        self.__axes = self.__figure.add_subplot(111)

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

        self.__max_time = QLineEdit("")
        self.connect(self.__max_time, SIGNAL("returnPressed()"), self.updateMaxTime)

        labeled_input = QVBoxLayout()
        labeled_input.addWidget(QLabel("Max time"))
        labeled_input.addWidget(self.__max_time)
        hbox.addLayout(labeled_input, stretch=1)

    def updateMaxTime(self):
        try:
            maxtime = float(self.__max_time.text())
            self.__user_set_max_time = maxtime
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
        
        self.__bandwidth_up_toggle = QRadioButton("Bandwidth (up)")
        self.__latency_toggle = QRadioButton("Latency")
        
        self.__latency_toggle.setChecked(True)
        self.connect(self.__bandwidth_up_toggle, SIGNAL("toggled(bool)"), self.on_draw)
        self.connect(self.__latency_toggle, SIGNAL("toggled(bool)"), self.on_draw)

        toggles = QVBoxLayout()
        toggles.addWidget(self.__bandwidth_up_toggle)
        toggles.addWidget(self.__latency_toggle)
        hbox.addLayout(toggles)
        
    def on_draw(self):
        self.setWindowTitle(self.__title)

        self.__axes.clear()
        self.__getSessionAxes().clear()

        if self.__measurements_only:
            self.__plotTrace()
            self.__plotMeasurements()
            self.__drawRedundancyDecisions()
        else:
            self.__setupAxes()
            self.__setTraceEnd()
            self.__drawWifi()
            self.__drawIROBs()
            self.__drawSessions()
            self.__drawRedundancyDecisions()

            if self.__show_debugging.isChecked():
                self.__drawDebugging()
        
        self.__canvas.draw()

    def __whatToPlot(self):
        if self.__bandwidth_up_toggle.isChecked():
            return 'bandwidth_up'
        elif self.__latency_toggle.isChecked():
            return 'latency'
        else: assert False

    def __getYAxisLabel(self):
        labels = {'bandwidth_up': 'Bandwidth (up) (bytes/sec)',
                  'latency': 'RTT (seconds)'}
        return labels[self.__whatToPlot()]

    class NetworkTrace(object):
        def __init__(self, trace_file, window, estimates):
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
                return ("%s_%s" % (net_type, value_type))

            def pairs():
                for network_type, what_to_plot in product(estimates, value_names):
                    yield network_type, what_to_plot
            self.__pairs = pairs

            last_estimates = {}
            for network_type, what_to_plot in self.__pairs():
                last_estimate_time = estimates[network_type][what_to_plot][-1]['timestamp']
                last_estimates[network_type] = last_estimate_time

            for network_type, what_to_plot in self.__pairs():
                key = getTraceKey(network_type, what_to_plot)
                last_estimate_time = window.getAdjustedTime(last_estimates[network_type])
                times = self.__priv_trace.getData('start', 0, last_estimate_time)
                values = self.__priv_trace.getData(key, 0, last_estimate_time)
                
                self.__start = times[0]
                self.__timestamps[network_type][what_to_plot] = \
                    map(lambda x: x-self.__start, times)
                self.__values[network_type][what_to_plot] = \
                    map(lambda x: x * conversions[what_to_plot], values)

            self.__computeVariance(self.__values)

        def __computeVariance(self, values):
            # values dictionary is populated in __init__
            self.__upper_variance_values = {}
            self.__lower_variance_values = {}
            for network_type, what_to_plot in self.__pairs():
                values = self.__values[network_type][what_to_plot]
                variances = stepwise_variance(values)
                std_devs = [v ** .5 for v in variances]

                uppers = [v + stddev for v, stddev in zip(values, std_devs)]
                lowers = [v - stddev for v, stddev in zip(values, std_devs)]

                if network_type not in self.__upper_variance_values:
                    self.__upper_variance_values[network_type] = {}
                    self.__lower_variance_values[network_type] = {}
                self.__upper_variance_values[network_type][what_to_plot] = uppers
                self.__lower_variance_values[network_type][what_to_plot] = lowers

        plot_colors = {'wifi': (.7, .7, 1.0), '3G': (1.0, .7, .7)}

        # whiten up the colors for the variance plotting
        variance_colors = {name: map(lambda v: v + ((1.0 - v) * 0.5), color)
                        for name, color in plot_colors.items()}
        
        def __plot(self, axes, what_to_plot, checks, values, colors, labeler):
            for network_type in self.__timestamps:
                if (checks[network_type].isChecked()):
                    axes.plot(self.__timestamps[network_type][what_to_plot],
                              values[network_type][what_to_plot],
                              color=colors[network_type],
                              label=labeler(network_type))

        def plot(self, axes, what_to_plot, checks):
            self.__plot(axes, what_to_plot, checks, self.__values,
                        type(self).plot_colors,
                        lambda network_type: network_type + " trace")

        def plotVariance(self, axes, what_to_plot, checks):
            self.__plot(axes, what_to_plot, checks, self.__upper_variance_values,
                        type(self).variance_colors, lambda x: None)
            self.__plot(axes, what_to_plot, checks, self.__lower_variance_values,
                        type(self).variance_colors, lambda x: None)


    def __plotTrace(self):
        if self.__network_trace_file and not self.__trace:
            cls = IntNWBehaviorPlot.NetworkTrace
            self.__trace = cls(self.__network_trace_file, self, self.__estimates)
            
        checks = {'wifi': self.__show_wifi, '3G': self.__show_threeg}
        if self.__show_trace.isChecked():
            self.__trace.plot(self.__axes, self.__whatToPlot(), checks)
            if self.__show_trace_variance.isChecked():
                self.__trace.plotVariance(self.__axes, self.__whatToPlot(), checks)
        self.__axes.set_ylim(0.0, self.__axes.get_ylim()[1])

    def __plotMeasurements(self):
        markers = {'wifi': 's', '3G': 'o'}
        checks = {'wifi': self.__show_wifi, '3G': self.__show_threeg}

        what_to_plot = self.__whatToPlot()
        
        for network_type in self.__estimates:
            if not checks[network_type].isChecked():
                continue
            
            estimates = self.__estimates[network_type][what_to_plot]
            times = [self.getAdjustedTime(e['timestamp']) for e in estimates]

            txform = 1.0
            if what_to_plot == 'latency':
                # RTT = latency * 2
                txform = 2.0
            
            observations = [e['observation'] * txform for e in estimates]
            estimated_values = [e['estimate'] * txform for e in estimates]

            # shift estimtates one to the right, so we're plotting
            #  each observation at the same time as the PREVIOUS estimate
            #  (this visualizes the error samples that the decision algorithm uses)
            estimated_values = [estimated_values[0]] + estimated_values[:-1]

            color = self.__irob_colors[network_type]
            if self.__show_measurements.isChecked():
                self.__axes.plot(times, estimated_values,
                                 label=network_type + " prev-estimates",
                                 color=color)
                self.__axes.plot(times, observations, label=network_type + " observations",
                                 linestyle='none', marker=markers[network_type],
                                 markersize=3, color=color)
            self.__axes.set_xlabel("Time (seconds)")
            self.__axes.set_ylabel(self.__getYAxisLabel())
            if self.__show_legend.isChecked():
                self.__axes.legend()

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

    def __getSessionAxes(self):
        if self.__session_axes == None:
            self.__session_axes = self.__axes.twinx()
        if self.__user_set_max_time:
            self.__session_axes.set_ylim(0.0, self.__user_set_max_time)
        return self.__session_axes

    def __drawSessions(self, **kwargs):
        if self.__sessions and self.__show_sessions.isChecked():
            self.__drawSomeSessions(self.__sessions, **kwargs)

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
        self.__axes.broken_barh(bars, height, color="green", alpha=0.3)

    def printStats(self):
        if self.__choose_network_calls:
            print ("%f seconds in chooseNetwork (%d calls)" %
                   (sum(self.__choose_network_calls), len(self.__choose_network_calls)))

        self.__printRedundancyBenefitAnalysis()

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

            dprint("Session: %s" % session)
            dprint("  IROBs: " % irobs)

            dropped_irobs = filter(lambda x: x.wasDropped(), irobs)
            return len(dropped_irobs) > 0

        failover_sessions = filter(failed_over, self.__sessions)
        print ("Failover sessions: %d/%d (%.2f%%), total %f seconds" %
               (len(failover_sessions), num_sessions,
                len(failover_sessions)/float(num_sessions) * 100,
                sum([duration(s) for s in failover_sessions])))
        
        self.__debug_sessions = failover_sessions

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

        self.__debug_sessions = reevaluation_sessions

    def __getIROBs(self, start, end):
        """Get all IROBs that start in the specified time range.

        Returns a dictionary: d[network_type][direction] => [IROB(),...]

        start -- relative starting time
        end -- relative ending time

        """
        matching_irobs = {}
        def time_matches(irob):
            irob_start, irob_end = [self.getAdjustedTime(t) for t in irob.getDuration()]
            return (irob_start >= start and irob_end <= end)
            
        for network_type, direction in product(['wifi', '3G'], ['down', 'up']):
            irobs = self.__networks[network_type][direction].values()
            matching_irobs[network_type] = {}
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

    def parseLine(self, line):
        timestamp = getTimestamp(line)
        if self.__start == None:
            self.__start = timestamp
            
        self.__end = timestamp

        if "Got update from scout" in line:
            #[time][pid][tid] Got update from scout: 192.168.1.2 is up,
            #                 bandwidth_down 43226 bandwidth_up 12739 bytes/sec RTT 97 ms
            #                 type wifi
            self.__modifyNetwork(line)
        elif "Successfully bound" in line:
            # [time][pid][CSockSender 57] Successfully bound osfd 57 to 192.168.1.2:0
            self.__addConnection(line)
        elif "Adding connection" in line:
            # [time][pid][Listener 13] Adding connection 14 from 192.168.1.2
            #                          bw_down 43226 bw_up 12739 RTT 97
            #                          type wifi(peername 141.212.110.115)
            self.__addIncomingConnection(line)
        elif "Adding connection" in line:
            # [time][pid][Listener 13] Adding connection 14 from 192.168.1.2
            #                          bw_down 244696 bw_up 107664 RTT 391
            #                          type wifi(peername 141.212.110.115)
            pass # No accepting-side log analysis yet.
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
            if network_type not in self.__estimates:
                self.__estimates[network_type] = {}
                
            dprint("got observation: %s" % line)
            bw_match = re.search(self.__network_bandwidth_regex, line)
            lat_match = re.search(self.__network_latency_regex, line)
            for match, name in zip((bw_match, lat_match), ("bandwidth_up", "latency")):
                if match:
                    obs, est = match.groups()
                    all_estimates = self.__estimates[network_type]
                    if name not in all_estimates:
                        all_estimates[name] = []
                    estimates = all_estimates[name]
                    estimates.append({'timestamp': float(timestamp),
                                      'observation': float(obs),
                                      'estimate': float(est)})
        elif "chooseNetwork" in line:
            duration = timestamp - getTimestamp(self.__last_line)
            self.__choose_network_calls.append(duration)
        elif "redundancy_strategy_type" in line:
            # [timestamp][pid][Bootstrapper 49] Sending hello:  Type: Hello(0)
            #                                   Send labels:  listen port: 42424
            #                                   num_ifaces: 2 
            #                                   redundancy_strategy_type: intnw_redundant
            redundancy_strategy = \
                re.search(self.__redundancy_strategy_regex, line).group(1)
            self.__title = "IntNW - " + redundancy_strategy + (" - Run %d" % self.__run)
        else:
            pass # ignore it
            
        self.__last_line = line

    def __initRegexps(self):
        self.__irob_regex = re.compile("IROB: ([0-9]+)")
        self.__datalen_regex = re.compile("datalen: ([0-9]+)")
        self.__expected_bytes_regex = re.compile("expected_bytes: ([0-9]+)")
        self.__network_regex = re.compile("scout: (.+) is (down|up).+ type ([A-Za-z0-9]+)")
        self.__ip_regex = re.compile("([0-9]+(?:\.[0-9]+){3})")
        self.__socket_regex = re.compile("\[CSock(?:Sender|Receiver) ([0-9]+)\]")
        self.__intnw_message_type_regex = \
            re.compile("(?:About to send|Received) message:  Type: ([A-Za-z_]+)")
        self.__csocket_destroyed_regex = re.compile("CSocket (.+) is being destroyed")
        self.__network_estimator_regex = \
            re.compile("Adding new stats to (.+) network estimator")

        float_regex = "([0-9]+\.[0-9]+)"
        stats_regex = "obs %s est %s" % (float_regex, float_regex)
        self.__network_bandwidth_regex = re.compile("bandwidth: " + stats_regex)
        self.__network_latency_regex = re.compile("latency: " + stats_regex)

        self.__redundancy_strategy_regex = \
            re.compile("redundancy_strategy_type: ([a-z_]+)\s*")
        
    def __getIROBId(self, line):
        return int(re.search(self.__irob_regex, line).group(1))

    def __getSocket(self, line):
        return int(re.search(self.__socket_regex, line).group(1))

    def __getIP(self, line):
        return re.search(self.__ip_regex, line).group(1)

    def __modifyNetwork(self, line):
        timestamp = getTimestamp(line)
        ip, status, network_type = re.search(self.__network_regex, line).groups()
        if network_type not in self.__networks:
            self.__networks[network_type] = {
                'down': {}, # download IROBs
                'up': {}    # upload IROBs
                }
            self.__network_periods[network_type] = []

        if status == 'down':
            period = self.__network_periods[network_type][-1]
            assert period['end'] == None
            period['end'] = timestamp
            
            assert ip in self.__network_type_by_ip
            del self.__network_type_by_ip[ip]
        elif status == 'up':
            periods = self.__network_periods[network_type]
            if len(periods) > 0 and periods[-1]['end'] == None:
                # two perfectly adjacent periods with no 'down' in between.  whatevs.
                periods[-1]['end'] = timestamp
                
            periods.append({
                'start': timestamp, 'end': None,
                'ip': ip, 'sock': None
                })

            placeholder = (ip in self.__network_type_by_ip and
                           self.__network_type_by_ip[ip] == "placeholder")
            self.__network_type_by_ip[ip] = network_type
            if placeholder:
                sock = self.__placeholder_sockets[ip]
                del self.__placeholder_sockets[ip]
                self.__addNetworkPeriod(sock, ip)
        else: assert False
        
    def __addConnection(self, line):
        sock = self.__getSocket(line)
        ip = self.__getIP(line)
        if ip not in self.__network_type_by_ip:
            self.__network_type_by_ip[ip] = "placeholder"
            assert ip not in self.__placeholder_sockets
            self.__placeholder_sockets[ip] = sock
        else:
            self.__addNetworkPeriod(sock, ip)

    def __addNetworkPeriod(self, sock, ip):
        network_type = self.__network_type_by_ip[ip]
        network_period = self.__network_periods[network_type][-1]

        assert network_period['start'] != None
        assert network_period['ip'] == ip
        assert network_period['sock'] == None
        network_period['sock'] = sock

        assert sock not in self.__network_type_by_sock
        self.__network_type_by_sock[sock] = network_type

    def __addIncomingConnection(self, line):
        raise NotImplementedError() # TODO
        
    def __removeConnection(self, line):
        timestamp = getTimestamp(line)
        sock = int(re.search(self.__csocket_destroyed_regex, line).group(1))
        if sock in self.__network_type_by_sock:
            network_type = self.__network_type_by_sock[sock]
            network_period = self.__network_periods[network_type][-1]
            
            network_period['sock'] = None
            del self.__network_type_by_sock[sock]

            self.__markDroppedIROBs(timestamp, network_type)

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
    
class IntNWPlotter(object):
    def __init__(self, args):
        self.__windows = []
        self.__currentPid = None
        self.__pid_regex = re.compile("^\[[0-9]+\.[0-9]+\]\[([0-9]+)\]")
        self.__session_regex = re.compile("start ([0-9]+\.[0-9]+)" + 
                                          ".+duration ([0-9]+\.[0-9]+)")

        self.__measurements_only = args.measurements
        self.__network_trace_file = args.network_trace_file
        self.__intnw_log = args.basedir + "/intnw.log"
        self.__trace_replayer_log = args.basedir + "/trace_replayer.log"
        self.__redundancy_eval_log = args.basedir + "/instruments.log"
        
        self.__readFile(self.__intnw_log)
        self.draw()
        self.printStats()

    def draw(self):
        for window in self.__windows:
            window.on_draw()

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
        for linenum, line in enumerate(open(filename).readlines()):
            fields = line.strip().split()
            if "Redundancy strategy" in line:
                runs.append([])
            elif "Session times:" in line:
                # start over with the better list of sessions
                runs[-1] = []
            elif "  Session" in line:
                timestamp, duration = self.__getSession(line)
                transfer = {'start': timestamp, 'end': timestamp + duration}

                sessions = runs[-1]
                sessions.append(transfer)
            else:
                try:
                    timestamp = float(fields[0])
                except ValueError:
                    continue

            sessions = runs[-1]
            if "Executing:" in line and fields[2] == "at":
                # start of a session
                transfer = {'start': timestamp, 'end': None}
                sessions.append(transfer)
            elif (("Waiting to execute" in line and fields[4] == "at") or
                  "Waiting until trace end" in line):
                  # end of a session
                if len(sessions) > 0:
                    sessions[-1]['end'] = timestamp
        return runs

    def __readRedundancyDecisions(self):
        filename = self.__redundancy_eval_log
        if not os.path.exists(filename):
            return
        
        runs = []
        last_pid = 0
        for linenum, line in enumerate(open(filename).readlines()):
            pid = self.__getPid(line)
            if pid != last_pid:
                runs.append([])
            last_pid = pid

            current_run = runs[-1]
            
            timestamp = getTimestamp(line)
            fields = line.strip().split()
            if "Redundant strategy benefit" in line:
                decision = RedundancyDecision(timestamp)
                decision.benefit = float(fields[4])
                dprint("Redundancy benefit: %f" % decision.benefit)
                
                current_run.append(decision)
            elif "Redundant strategy cost" in line:
                decision = current_run[-1]
                decision.cost = float(fields[4])
                dprint("Redundancy cost: %f" % decision.cost)
        return runs

    def __readFile(self, filename):
        session_runs = None
        redundancy_decisions = None
        if self.__trace_replayer_log:
            session_runs = self.__readSessions()
        if self.__redundancy_eval_log:
            redundancy_decisions_runs = self.__readRedundancyDecisions()
        
        print "Parsing log file..."
        progress = ProgressBar()
        for linenum, line in enumerate(progress(open(filename).readlines())):
            try:
                pid = self.__getPid(line)
                if pid == None:
                    continue
                    
                if pid != self.__currentPid:
                    self.__windows.append(IntNWBehaviorPlot(len(self.__windows) + 1,
                                                            self.__measurements_only,
                                                            self.__network_trace_file))
                    self.__currentPid = pid
                    window_num = len(self.__windows) - 1
                    if session_runs:
                        sessions = session_runs[window_num]
                        self.__windows[-1].setSessions(sessions)
                    if redundancy_decisions_runs:
                        redundancy_decisions = redundancy_decisions_runs[window_num]
                        self.__windows[-1].setRedundancyDecisions(redundancy_decisions)
                    
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
    
def main():
    parser = ArgumentParser()
    parser.add_argument("basedir")
    parser.add_argument("--measurements", action="store_true", default=False)
    parser.add_argument("--network-trace-file", default=None)
    parser.add_argument("--noplot", action="store_true", default=False)
    args = parser.parse_args()

    app = QApplication(sys.argv)
    
    plotter = IntNWPlotter(args)
    if not args.noplot:
        plotter.show()
        app.exec_()

if __name__ == '__main__':
    main()
