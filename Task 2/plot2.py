import numpy as np
import matplotlib.pyplot as plt
from matplotlib.pyplot import figure
import sys
import csv
from argparse import ArgumentParser
import re
from math import floor

def scale(a):
    return a/1000000.0

parser = ArgumentParser(description="plot")

parser.add_argument('--dir', '-d',
                    help="Directory to store outputs",
                    required=True)

parser.add_argument('--name', '-n',
                    help="name of the experiment",
                    required=True)

# parser.add_argument('--trace', '-tr',
#                     help="name of the trace",
#                     required=True)

args = parser.parse_args()

data = open(args.dir+"/"+str(args.name), 'r')
cnt = 0
cntList = []
cwndList = []
cwndBefore = -1
for line in data:
    dataLine = line.strip().split(",")
    cwndValue = dataLine[1]
    if floor(float(cwndValue)) == cwndBefore:
        continue
    cwndBefore = floor(float(cwndValue))
    cnt+= 1 
    cntList.append(cnt)
    cwndList.append(int(floor(float(cwndValue))))
    
f = plt.figure()
f.set_figwidth(30)
f.set_figheight(2)

#window
plt.plot(cntList, cwndList, label = "CWND")
# #thresh
# plt.plot(data['time'], data['ssthresh'], label = "SS Threshold")
plt.legend()

plt.xlabel("Count of cwnd")
plt.xlim([0,300])
plt.title("CWND")
plt.savefig(args.dir+'/cwndGraph2.png',dpi=150,bbox_inches='tight')

