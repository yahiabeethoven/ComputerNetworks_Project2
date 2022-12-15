import numpy as np
import matplotlib.pyplot as plt
from matplotlib.pyplot import figure
import sys
import csv
from argparse import ArgumentParser
import re
from math import floor

# this program plots the change in value of cwnd over time
# rather than having time in seconds as the x-axis, we have an incremental count of every time cwnd has changed
def scale(a):
    return a/1000000.0

parser = ArgumentParser(description="plot")

parser.add_argument('--dir', '-d',
                    help="Directory to store outputs",
                    required=True)

parser.add_argument('--name', '-n',
                    help="name of the experiment",
                    required=True)

args = parser.parse_args()

data = open(args.dir+"/"+str(args.name), 'r')
cnt = 0
cntList = []
cwndList = []
cwndBefore = -1

# store an integer value of each cwnd, and replace each timestamp with an incremental index counter for each time the window value changed
for line in data:
    dataLine = line.strip().split(",")
    cwndValue = dataLine[1]
    cwndBefore = floor(float(cwndValue))
    cnt+= 1 
    cntList.append(cnt)
    cwndList.append(int(floor(float(cwndValue))))
    
f = plt.figure()
f.set_figwidth(30)
f.set_figheight(2)

#window
plt.plot(cntList, cwndList, label = "CWND")
plt.legend()

plt.xlabel("Count of cwnd")
# only print the first 300 because the count is too large to be visible
plt.xlim([0,300])
plt.title("Change in CWND Over Time Count")
plt.savefig(args.dir+'/cwndGraph1.png',dpi=150,bbox_inches='tight')

