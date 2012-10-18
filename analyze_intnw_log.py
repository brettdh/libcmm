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

import sys
from argparse import ArgumentParser

from PyQt4.QtCore import *
from PyQt4.QtGui import *

import matplotlib
from matplotlib.backends.backend_qt4agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.backends.backend_qt4agg import NavigationToolbar2QTAgg as NavigationToolbar
from matplotlib.figure import Figure

from progressbar import ProgressBar

class LogParsingError(Error):
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

class IntNWPlotter(QMainWindow):
    def __init__(self, filename, parent=None):
        QMainWindow.__init__(self, parent)
        
        self.__transfers = []
        self.__wifiPeriods = []
        self.__connections = {} # socket fd -> network type
        self.__readFile(filename)

        # TODO: infer plot title from file path
        self.__title = "IntNW"
        self.setWindowTitle(self.__title)

        self.create_main_frame()
        self.on_draw()

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

        widgets = [] # TODO: add any GUI widgets if needed
        for w in widgets:
            hbox.addWidget(w)
            hbox.setAlignment(w, Qt.AlignVCenter)
        
        vbox = QVBoxLayout()
        vbox.addWidget(self.__canvas)
        vbox.addWidget(self.__mpl_toolbar)
        vbox.addLayout(hbox)
        
        self.__frame.setLayout(vbox)
        self.setCentralWidget(self.__frame)

    def on_draw(self):
        self.__axes.clear()

        # TODO: draw the stuff.
        
        self.__canvas.draw()

    def __readFile(self, filename):
        print "Parsing log file..."
        progress = ProgressBar()
        for linenum, line in progress(enumerate(open(filename).readlines())):
            try:
                self.__parseLine(line)
            except LogParsingError as e:
                e.linenum = linenum
                e.line = line
                raise e
            except Error as e:
                e = LogParsingError(str(e))
                e.linenum = linenum
                e.line = line
                raise e

    def __parseLine(self, line):
        if "Got update from scout" in line:
            #[time][pid][tid] Got update from scout: 192.168.1.2 is up,
            #                 bandwidth_down 43226 bandwidth_up 12739 bytes/sec RTT 97 ms
            #                 type wifi
            self.__addNetwork(line)
        elif "Successfully bound" in line:
            # [time][pid][CSockSender 57] Successfully bound osfd 57 to 192.168.1.2:0
            self.__addConnection(line)
        elif "Adding connection" in line:
            # [time][pid][Listener 13] Adding connection 14 from 192.168.1.2
            #                          bw_down 244696 bw_up 107664 RTT 391
            #                          type wifi(peername 141.212.110.115)
            pass # No accepting-side log analysis yet.
        elif "Getting bytes to send from IROB" in line:
            # [time][pid][CSockSender 57] Getting bytes to send from IROB 6
            irob = int(line.strip().split()[-1])
            network = self.__getNetwork(line)
            self.__currentSendingIROB = irob
            self.__addIROB(network, irob, download=False)
        elif "...returning " in line:
            # [time][pid][CSockSender 57] ...returning 1216 bytes, seqno 0
            assert self.__currentSendingIROB != None
            datalen = int(line.strip().split()[3])
            network = self.__getNetwork(line)
            self.__addIROBBytes(network, self.__currentSendingIROB, datalen)
        elif "About to send message" in line:
            # [time][pid][CSockSender 57] About to send message:  Type: Begin_IROB(1)
            #                             Send labels: FG,SMALL IROB: 0 numdeps: 0
            self.__addTransfer(line, download=False)
        elif "Received message" in line:
            # [time][pid][CSockReceiver 57] Received message:  Type: Begin_IROB(1)
            #                               Send labels: FG,SMALL IROB: 0 numdeps: 0
            self.__addTransfer(line, download=True)
        else:
            pass # ignore it

    def __initRegexps(self):
        self.__irob_regex = re.compile("IROB: ([0-9]+)")
        self.__datalen_regex = re.compile("datalen: ([0-9]+)")
        self.__type_regex = re.compile("Type: ([A-Za-z_]+)")
        self.__expected_bytes_regex = re.compile("expected_bytes: ([0-9]+)")

    def __getIROB(self, line):
        return int(re.search(self.__irob_regex, line).group(1))
        
    def __getNetwork(self, line):
        # TODO
        pass

    def __getDatalen(self, line):
        return int(re.search(self.__datalen_regex, line).group(1))

    def __getMessageType(self, line):
        return re.search(self.__type_regex, line).group(1)
    
    def __getExpectedBytes(self, line):
        return int(re.search(self.__expected_bytes_regex, line).group(1))

    def __addTransfer(line, download):
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
        irob = self.__getIROB(line)
        network = self.__getNetwork(line)
        type = self.__getType(line)

        if type == "Begin_IROB":
            self.__currentSendingIROB = None
            self.__addIROB(network, irob, download)
        elif type == "IROB_chunk":
            datalen = self.__getDatalen(line)
            self.__addIROBBytes(network, irob, datalen, download)b
        elif type == "End_IROB" and download:
            expected_bytes = self.__getExpectedBytes(line)
            self.__finishReceivedIROB(network, irob, expected_bytes)
        elif type == "Ack":
            if download:
                self.__ackIROB(network, irob)
        else:
            pass # ignore other types of messages

    def __addIROB(self, network, irob_id, download):
        if network not in self.__networks:
            raise LogParsingError("saw data on unknown network '%s'" % network)
        irobs = self.__networks[network][download]
        if irob_id not in irobs:
            irobs[irob_id] = IROB(irob_id)

    def __addIROBBytes(self, network, irob_id, datalen, download):
        # TODO
        pass

    def __finishReceivedIROB(self, network, irob_id, expected_bytes):
        # TODO
        pass

    def __ackIROB(self, network, irob_id):
        # TODO
        pass
    
    
def main():
    parser = ArgumentParser()
    parser.add_argument("filename")
    args = parser.parse_args()

    app = QApplication(sys.argv)
    
    plotter = IntNWPlotter(args.filename)
    plotter.show()
    app.exec_()

if __name__ == '__main__':
    main()
