import numpy as np
import matplotlib.pyplot as plt
import sys
from argparse import ArgumentParser

# this program plots the graph for the throughput in Mbps over time in seconds as given to us by the professor

def scale(a):
    return a/1000000.0

parser = ArgumentParser(description="plot")

parser.add_argument('--dir', '-d',
                    help="Directory to store outputs",
                    required=True)

parser.add_argument('--name', '-n',
                    help="name of the experiment",
                    required=True)

parser.add_argument('--trace', '-tr',
                    help="name of the trace",
                    required=True)

args = parser.parse_args()

fig = plt.figure(figsize=(21,3), facecolor='w')
ax = plt.gca()


# plotting throughput
throughputDL = []
timeDL = []

traceDL = open (args.dir+"/"+str(args.name), 'r')
traceDL.readline()

tmp = traceDL.readline().strip().split(",")
# since the value of cwnd is a float in our code, we have to type cast it first before using it
bytes = int(float(tmp[1]))
startTime = float(tmp[0])
stime=float(startTime)

for time in traceDL:
    if (float(time.strip().split(",")[0]) - float(startTime)) <= 1.0:
        bytes += int(float(time.strip().split(",")[1]))
    else:
        throughputDL.append(bytes*8/1000000.0)
        timeDL.append(float(startTime)-stime)
        bytes = int(float(time.strip().split(",")[1]))
        startTime += 1.0

print (timeDL)
print (throughputDL)

plt.plot(timeDL, throughputDL, lw=2, color='r')

plt.ylabel("Throughput (Mbps)")
plt.xlabel("Time (s)")
plt.grid(True, which="both")
plt.savefig(args.dir+'/throughput.pdf',dpi=1000,bbox_inches='tight')