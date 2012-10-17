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
# 6) Annotations: popup box on hover (wxPython?)


# Lines of the log file I'm interested in:

# [1350338515.243048][18001][002a4940] Got update from scout: 192.168.1.2 is up, bandwidth_down 43226 bandwidth_up 12739 bytes/sec RTT 97 ms type wifi

# [1350338515.263536][18001][CSockSender 57] Successfully bound osfd 57 to 192.168.1.2:0

# [1350486872.277242][23478][Listener 13] Adding connection 14 from 192.168.1.2 bw_down 244696 bw_up 107664 RTT 391 type wifi(peername 141.212.110.115)

# [1350338524.776829][18001][CSockSender 57] About to send message:  Type: Begin_IROB(1) Send labels: FG,SMALL IROB: 0 numdeps: 0

# [1350338524.829664][18001][CSockReceiver 57] Received message:  Type: Begin_IROB(1) Send labels: FG,SMALL IROB: 0 numdeps: 0

# [1350338524.831890][18001][CSockReceiver 57] Received message:  Type: IROB_chunk(3) Send labels: FG,SMALL IROB: 0 seqno: 0 offset: 0 datalen: 13

# [1350338524.835000][18001][CSockReceiver 57] Received message:  Type: End_IROB(2) Send labels: FG,SMALL IROB: 0 expected_bytes: 13 expected_chunks: 1

# [1350338524.811798][18001][CSockReceiver 57] Received message:  Type: Ack(7) Send labels:  num_acks: 0 IROB: 0 srv_time: 0.000997 qdelay: 0.000000


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

        widgets = [] # TODO: add all my GUI widgets
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
        for line in progress(open(filename).readlines()):
            self.__parseLine(line)

    def __parseLine(self, line):
        parser = self.__getParser(line)
        parser.parse(line)

    def __getParser(self, line):
        parser = IntNWPlotter._IgnoreParser()
        if "Got update from scout" in line:
            pass
        elif "Successfully bound" in line:
            pass
        elif "" in line: # TODO: more parsers
            pass

        parser.plotter = self
        return parser

    class _LineParser(object):
        def parse(self, line):
            raise NotImplementedError()

    class _IgnoreParser(_LineParser):
        def parse(self, line):
            pass # ignore it

    class _ConnectionParser(_LineParser):
        def parse(self, line):
            pass # TODO
    
    # TODO: more parsers

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
