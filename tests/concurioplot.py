#!/usr/bin/env python

from collections import defaultdict
import sys

import pylab

infile = sys.stdin
if len(sys.argv) > 1:
    infile = open(sys.argv[1])

mode = infile.readline().strip()

lines = defaultdict(list)
for line in infile:
    id, value = map(int, line.split(': '))
    lines[id].append(value)

if mode == 'write-time':
    for ydata in lines.values():
        pylab.plot(ydata, '+-')
else:
    for id, xdata in lines.iteritems():
        if id == 0:
            for x in xdata:
                pylab.axvline(x, color='k')
        pylab.plot(xdata, [id for _ in xdata], '+')
    pylab.ylim(-1, len(lines))

if len(sys.argv) > 1 and sys.argv[1] == '-s':
    pylab.savefig('concurioplot.png', dpi=100)
else:
    pylab.show()
