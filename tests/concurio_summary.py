#!/usr/bin/python

from collections import defaultdict
from math import sqrt
import sys

from stats2 import Statistics

ci = {0.800: 1.28155,
      0.900: 1.64485,
      0.950: 1.95996,
      0.990: 2.57583,
      0.995: 2.80703,
      0.999: 3.29053}
def get_ci(stddev, N):
    return ci[0.95] * stddev / sqrt(N)

crop_width = 100
if len(sys.argv) > 1:
    crop_width = sys.argv[1]
infile = sys.stdin
if len(sys.argv) > 2:
    infile = open(sys.argv[2])

mode = infile.readline().strip()

lines = defaultdict(list)
for line in infile:
    id, value = map(int, line.split(': '))
    lines[id].append(value)

if mode == 'write-time':
    means = []
    for line in lines.values():
        s = Statistics(line[crop_width:len(line) - crop_width])
        means.append(s.mean)
    s = Statistics(means)
    sys.stdout.write('%g, %g, %g, %g\n' % (s.mean, s.stddev, s.N, get_ci(s.stddev, s.N)))
else:
    sys.exit(1)
