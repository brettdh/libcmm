import sys
import re

class FakeNumpy(object):
    def mean(self, list):
        return (sum(list)/len(list))

    def std(self, list):
        avg = self.mean(list)
        sqdevs = [(num - avg)*(num - avg) for num in list]
        return math.sqrt(sum(sqdevs)/len(list))

try:
    import numpy
except ImportError, e:
    numpy = FakeNumpy()

class ExperimentData:
    def __init__(self):
        self.runs = []
        self.curSenderPID = 0
        self.response_times = []
        self.avgs = {
            "vanilla" : {
                "foreground" : [],
                "background" : []
                },
            "intnw" : {
                "foreground" : [],
                "background" : []
                }
            }
        
    #/Results for run [0-9]+/ {
    def newRun(self, line):
        self.runs.append({
                "vanilla" : {
                    "foreground" : [],
                    "background" : []
                    },
                "intnw" : {
                    "foreground" : [],
                    "background" : []
                    }
                })
        self.response_times = []
        self.curSenderPID = 0
        
    def addAverageAndClear(self):
        if self.curSenderPID != 0:
            if self.curSenderLabel == "foreground":
                # calculate average response time for this sender
                val = sum(self.response_times) / len(self.response_times)
            elif self.curSenderLabel == "background":
                # calculate total throughput for this sender
                val = (len(self.response_times) * self.chunksize) / sum(self.response_times)
            else:
                raise Exception("NOT_REACHED")

            self.runs[-1][self.curSenderType][self.curSenderLabel].append(val)
            self.response_times = []

    #/Worker PID [0-9]+ - .+ .+ sender results/ {
    def newSender(self, line):
        fields = line.split()
        self.curSenderPID = int(fields[2])
        self.curSenderType = fields[4]
        self.curSenderLabel = fields[5]

        if self.curSenderType not in ["vanilla", "intnw"]:
            raise Exception("Unknown sender type %s" % self.curSenderType)
        if self.curSenderLabel not in ["foreground", "background"]:
            raise Exception("Unknown sender label %s" % self.curSenderLabel)
        
    #/Chunksize: [0-9]+/ {
    def setChunksize(self, line):
        self.chunksize = int(line.split()[1])

    #/[0-9]+\.[0-9+]+ +[0-9]+\.[0-9+]+ +[0-9]+\.[0-9+]+/ {
    def newDataPoint(self, line):
        fields = line.split()
        response_time = float(fields[1])
        self.response_times.append(response_time)

    def figureString(self, label):
        if label == "foreground":
            return "response time"
        elif label == "background":
            return "total throughput"
        else: raise Exception("NOT_REACHED")
        
    def printSummary(self):
        if len(self.runs) > 0:
            print "\nRun-by-run breakdown:"
            for printWhat in ["count", "name", "figure"]:
                if printWhat == "count":
                    sys.stdout.write("%-12s" % "senders-->")
                elif printWhat == "name":
                    sys.stdout.write("%-12s " % " ")
                else:
                    sys.stdout.write("%-12s " % "Run")
                for type in self.runs[0].keys():
                    for label in self.runs[0][type].keys():
                        if printWhat == "count":
                            s = str(len(self.runs[0][type][label]))
                        elif printWhat == "name":
                            s = "%s+%s" % (type, label)
                        elif printWhat == "figure":
                            s = self.figureString(label)
                        sys.stdout.write("%18s " % s)
                sys.stdout.write("\n")
            
            for i, run in enumerate(self.runs):
                sys.stdout.write("%-12d" % (i+1))
                for type in run.keys(): # ['vanilla', 'intnw']
                    for label in run[type].keys(): # ['foreground', 'background']
                        figure = self.figureString(label)
                        count = len(run[type][label])
                        if count > 0:
                            val = sum(run[type][label]) / count
                            s = "%18f " % val
                            
                            # calculate the averages while we're at it
                            self.avgs[type][label].append(val)
                        else:
                            s = "%18s " % "---"
                        sys.stdout.write(s)
                        
                sys.stdout.write("\n")
            
            print "\nSummary of %d runs:" % len(self.runs)
            for type in self.avgs.keys():
                for label in self.avgs[type].keys():
                    if (len(self.avgs[type][label]) > 0):
                        figure = self.figureString(label)
                        avg = numpy.mean(self.avgs[type][label])
                        stddev = numpy.std(self.avgs[type][label])
                        print ("%s+%s: avg %s %f stddev %f" % (type, label, figure, 
                                                               avg, stddev))

def main():
    dataLinePattern = re.compile("[0-9]+\.[0-9+]+ +[0-9]+\.[0-9+]+ +[0-9]+\.[0-9+]+")

    data = ExperimentData()
    lines = sys.stdin.readlines()
    for line in lines:
        sys.stdout.write(line)

        if "Results for run" in line:
            data.addAverageAndClear()
            data.newRun(line)
        elif "Worker PID" in line:
            data.addAverageAndClear()
            data.newSender(line)
        elif "Chunksize" in line:
            data.setChunksize(line)
        elif dataLinePattern.match(line):
            data.newDataPoint(line)

    data.addAverageAndClear()

    data.printSummary()

if __name__ == '__main__':
    main()
