#!/usr/bin/env python2.7

def check_file_generic(f, get_num):
    print "Evaluating %s for non-monotonicity" % f
    
    lines = open(f).readlines()
    times = []
    for line in lines:
        t = get_num(line)
        if t is not None:
            times.append(get_num(line))
        
    # times is a list of (timestamp_string, timestamp_float) tuples
    for i in xrange(len(times)):
        if i > 0 and times[i][1] < times[i-1][1]:
            print "%s: %f" % (times[i-1][0], times[i][1] - times[i-1][1])

_unix_float_timestamp = "([0-9]+\.[0-9]+)"
import re

def check_file_intnw(f):
    def get_timestamp(line):
        m = re.search("^\[%s\]" % _unix_float_timestamp, line)
        if m:
            ts_str = m.group(1)
            return ts_str, float(ts_str)
        return None

    check_file_generic(f, get_timestamp)

def check_file_trace_replayer(f):
    def get_timestamp(line):
        try:
            ts_str = line.split()[0]
            m = re.search(_unix_float_timestamp, ts_str)
            if m:
                return ts_str, float(ts_str)
        except (ValueError, IndexError):
            pass # fall through
        return None
        
    check_file_generic(f, get_timestamp)

def check_file_timing(f):
    def get_timestamp(line):
        from dateutil import parser
        fields = line.strip().split()
        if len(fields) < 6:
            return None
        datestr = " ".join(fields)
        
        try:
            d = parser.parse(datestr)
            return datestr, time.mktime(d.timetuple())
        except ValueError:
            return None
    
    check_file_generic(f, get_timestamp);

from argparse import ArgumentParser
import os.path
import sys

if __name__ == '__main__':
    parser = ArgumentParser()
    parser.add_argument("filename")
    args = parser.parse_args()

    choices = ['intnw.log', 'trace_replayer.log', 'timing.log']
    
    # trim leading path
    file_base = os.path.basename(args.filename)
    if file_base not in choices:
        print "Invalid filename %s should be one of %s" % (file_base, choices)
        sys.exit(1)

    # trim .log extension
    func = locals()["check_file_" + file_base[:-4]]
    func(args.filename)
