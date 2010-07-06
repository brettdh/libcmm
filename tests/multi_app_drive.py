#!/usr/bin/python
import sys, os, time, signal, select
from optparse import OptionParser
from exceptions import Exception
from subprocess import Popen, PIPE
import random

def startScout():
    print "Starting scout"
    scout_path = "/home/brettdh/src/libcmm/"
    scout_bin = scout_path + "conn_scout"
    scout_log = open("./logs/scout.log", "a")
    scout = Popen([scout_bin, "replay", "eth1", "eth0"],
                  stderr=scout_log, stdout=scout_log)

    time.sleep(3) # Wait to make sure the scout is running
    if scout.poll() != None:
        print "conn_scout failed to start!"
        return None

    return scout

vs_log_filename = "./logs/vs.log"

if __name__ == '__main__':
    usage = "Usage: %prog <path/to/vs_bin> <script_file> <numruns>"
    parser = OptionParser(usage=usage)
    parser.set_defaults(start_scout=False)
    parser.add_option("-s", "--scout", action="store_true",
                      dest="start_scout",
                      help="Start the connection scout and run a trace.")
                      
    (options, args) = parser.parse_args()

    if (len(args) != 3):
        parser.print_help()
        sys.exit(1)

    numruns = int(args[2])
        
    for i in xrange(numruns):
        if options.start_scout:
            scout = startScout()
            if scout == None:
                print "Scout failed to start!"
                sys.exit(1)

        try:
            vs_bin = args[0]
            script_file = args[1]
            vs_output = open(vs_log_filename, "a")
            
            run_start = time.time()
            vs_output.write("Starting new run at %f\n" % run_start)
            vs_output.flush()
            print "Starting new run at %f" % run_start
            vs_pipe = Popen([vs_bin, "-c", "141.212.110.132",
                             "-f", script_file],
                            stderr=vs_output, stdout=vs_output)
            # wait for it to finish
            vs_pipe.wait()
            run_end = time.time()
            vs_output.write("Run ended at %f\n" % run_end)
            vs_output.close()
            print "Run ended at %f" % run_end

        except Exception, e:
            print "Error: ", str(e)
        finally:
            if options.start_scout:
                print "Killing scout"
                os.kill(scout.pid, 2)
                scout.wait()
        

    print "Finished %d runs" % numruns

    # replace, not append, because we append to all the files that
    #  the awk script reads from, so it will regenerate
    #  the runs that we replace
    results_filename = "./vs_results.out"
    results_file = open(results_filename, "w")
    vs_logfile_data = open(vs_log_filename).read()
    print "got %d bytes of timing output" % len(vs_logfile_data)
    awk = Popen(["awk", "-f", "./vs_process_results.awk"],
                stdin=PIPE, stdout=results_file)
    awk.communicate(vs_logfile_data)

    print "Processed results; wrote summary into %s" % results_filename
