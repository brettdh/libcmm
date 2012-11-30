#!/usr/bin/env python

def check_file_generic(f, get_num):
    print "Evaluating %s for non-monotonicity" % f
    
    lines = open(f).readlines()
    times = []
    for line in lines:
        t = get_num(line)
        if t is not None:
            times.append(get_num(line))
        
    for i,x in enumerate(times):
        if i > 0 and times[i] < times[i-1]:
            print "%f: %f" % (times[i-1], times[i] - times[i-1])

def check_file_intnw(f):
    def get_timestamp(line):
        import re
        m = re.search("^\[([0-9]+\.[0-9]+)\]", line)
        if m:
            return float(m.group(1))
        return None

    check_file_generic(f, get_timestamp)

def check_file_trace_replayer(f):
    def get_timestamp(line):
        try:
            return float(line.split()[0])
        except (ValueError, IndexError):
            return None
        
    check_file_generic(f, get_timestamp)


from argparse import ArgumentParser
import os.path
import sys

if __name__ == '__main__':
    parser = ArgumentParser()
    parser.add_argument("filename")
    args = parser.parse_args()

    choices = ['intnw.log', 'trace_replayer.log']
    
    # trim leading path
    file_base = os.path.basename(args.filename)
    if file_base not in choices:
        print "Invalid filename %s should be one of %s" % (file_base, choices)
        sys.exit(1)

    # trim .log extension
    func = locals()["check_file_" + file_base[:-4]]
    func(args.filename)
