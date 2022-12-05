import numpy as np
import matplotlib.pyplot as plt
import sys
from argparse import ArgumentParser
import re

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

data = np.genfromtxt(args.dir+"/"+str(args.name), delimiter=",", names=["time","window","ssthresh"])

f = plt.figure()
f.set_figwidth(30)
f.set_figheight(2)

#window
plt.plot(data['time'], data['window'], label = "CWND")
#thresh
plt.plot(data['time'], data['ssthresh'], label = "SS Threshold")
plt.legend()

plt.xlabel("Time (ms)")
plt.title("CWND and SS Threshold throughout the TCP Program")
plt.savefig(args.dir+'/cwndGraph.png',dpi=150,bbox_inches='tight')

# fig = plt.figure(figsize=(21,3), facecolor='w')
# ax = plt.gca()
#
#
# # plotting the trace file
# f1 = open (args.trace,"r")
# BW = []
# nextTime = 1000
# cnt = 0
# for line in f1:
#     #if int(float(line.strip())) > nextTime:
#     if int(float(re.split(r'[\"]?([0-9\.]*)[\"]?',line)[1])) > nextTime:
#         BW.append(cnt*1492*8)
#         cnt = 0
#         nextTime+=1000
#     else:
#         cnt+=1
# f1.close()
#
# ax.fill_between(range(len(BW)), 0, list(map(scale,BW)),color='#D3D3D3')
#
# # plotting throughput
# throughputDL = []
# timeDL = []
#
# traceDL = open (args.dir+"/"+str(args.name), 'r')
# traceDL.readline()
#
# tmp = traceDL.readline().strip().split(",")
# #bytes = int(tmp[1])
# #bytes = int(float(re.split(r'[\"]?([0-9\.]*)[\"]?',tmp[1])[1]))
# bytes = 6
# startTime = float(re.split(r'[\"]?([0-9\.]*)[\"]?',tmp[0])[1])
# stime=float(startTime)
#
# for time in traceDL:
#     if (float(re.split(r'[\"]?([0-9\.]*)[\"]?',time)[1]) - float(startTime)) <= 1.0:
#     #if (float(time.strip().split(",")[0]) - float(startTime)) <= 1.0:
#         #bytes += int(time.strip().split(",")[1])
#         bytes += int(float(re.split(r'[\"]?([0-9\.]*)[\"]?',time)[1]))
#     else:
#         throughputDL.append(bytes*8/1000000.0)
#         timeDL.append(float(startTime)-stime)
#         bytes = int(float(re.split(r'[\"]?([0-9\.]*)[\"]?',time)[1]))
#         startTime += 1.0
#
# print (timeDL)
# print (throughputDL)
#
# plt.plot(timeDL, throughputDL, lw=2, color='r')
#
# plt.ylabel("Throughput (Mbps)")
# plt.xlabel("Time (s)")
# # plt.xlim([0,300])
# plt.grid(True, which="both")
# plt.savefig(args.dir+'/throughput.pdf',dpi=1000,bbox_inches='tight')
