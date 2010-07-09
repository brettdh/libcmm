#!/usr/bin/python
import sys, os, time, signal, select
from exceptions import Exception
from subprocess import Popen, PIPE
import glob
import random

def startScout(scout_log):
    print "Starting scout"
    scout_path = "/home/brettdh/src/libcmm/scout/"
    #scout_path = "/home/brettdh/src/libcmm_single_process/scout/"
    scout_bin = scout_path + "conn_scout"
    scout = Popen([scout_bin, "replay", "eth1", "eth0"],
                  stderr=scout_log, stdout=scout_log)

    time.sleep(3) # Wait to make sure the scout is running
    if scout.poll() != None:
        print "conn_scout failed to start!"
        return None

    return scout

ma_log_filename = "./multi_app_test.log"

if __name__ == '__main__':
    if (len(sys.argv) < 2):
        print "Usage: multi_app_drive.py <numruns> <...arguments to multi_app_test...>"
        sys.exit(1)

    numruns = int(sys.argv[1])
    
    os.chdir('./results')
    os.mkdir('./drive_run_%d' % os.getpid())
    os.chdir('./drive_run_%d' % os.getpid())

    ma_output = open(ma_log_filename, "a")
    scout_log = open("./scout.log", "a")

    for i in xrange(numruns):
        scout = startScout(scout_log)
        if scout == None:
            print "Scout failed to start!"
            sys.exit(1)

        try:
            ma_bin = '../../../../multi_app_test'
            
            os.mkdir('./run_%d' % (i+1))
            os.chdir('./run_%d' % (i+1))
            run_start = time.time()
            ma_output.write("Starting new run at %f\n" % run_start)
            print "Starting new run at %f" % run_start
            ma_pipe = Popen([ma_bin,] + sys.argv[2:],
                            stderr=ma_output, stdout=ma_output)
            # wait for it to finish
            ma_pipe.wait()
            run_end = time.time()
            ma_output.write("Run ended at %f\n" % run_end)
            print "Run ended at %f" % run_end
            os.chdir('..')
        except Exception, e:
            print "Error: ", str(e)
        finally:
            print "Killing scout"
            os.kill(scout.pid, 2)
            scout.wait()
        
    ma_output.close()
    print "Finished %d runs" % numruns

    results_filename = "./multi_app_results.out"
    results_file = open(results_filename, "a")

    run_dirs = glob.glob('run_*')
    run_dirs.sort()
    run_num = 0
    ma_data = ""
    for run_dir in run_dirs:
        run_num += 1
        ma_data += ("Results for run %d\n" % run_num)
        os.chdir(run_dir)
        input_files = glob.glob('*.txt')
        input_files.sort()
        print "Processing files: ", input_files
        for input_file in input_files:
            ma_data += open(input_file).read()

        os.chdir('..')

    script = Popen(["python", "../../ma_process_results.py"],
                   stdin=PIPE, stdout=results_file)
    script.communicate(ma_data)

    os.chdir('..')
    print "Processed results; wrote summary into %s" % results_filename
