#!/usr/bin/env python3

import matplotlib.pyplot as plt
import seaborn as sns

# the following data was obtained with:
# repository commit: 
# MCAD command: 
# ./llvm-mcad -mtriple="x86_64-unknown-linux-gnu" -mcpu="skylake" --load-broker-plugin=plugins/qemu-broker/libMCADQemuBroker.so -broker-plugin-arg-host="localhost:9487" -enable-cache=0 -enable-branch-predictor=Naive -bht-size=32 -mispredict-delay=17
# MCAD (w/o BP) command: 
# ./llvm-mcad -mtriple="x86_64-unknown-linux-gnu" -mcpu="skylake" --load-broker-plugin=plugins/qemu-broker/libMCADQemuBroker.so -broker-plugin-arg-host="localhost:9487" -enable-cache=0 -enable-branch-predictor=None 
# QEMU command:
# ~/mcad/qemu/build/qemu-x86_64 -plugin ~/mcad/LLVM-MCA-Daemon/build/plugins/qemu-broker/Qemu/libQemuRelay.so,arg="-addr=127.0.0.1",arg="-port=9487" -d plugin ./bp_0  through ./bp_8

x_groups =                (   "0%",    "12%",   "25%",    "38%",    "50%",     "62%",   "75%",    "88%",   "100%")
cycle_data = {
    'perf':               ( 85_521,  108_893,  121_759,  141_260,  160_082,  185_161,  197_857,  227_740,  242_077),
    'MCAD':               ( 83_815,  126_959,  152_327,  177_008,  206_603,  228_926,  258_991,  285_141,  288_296),
    'MCAD (w/o BP)':      ( 83_338,   84_166,   84_528,   84_891,   85_267,   85_579,   85_920,   86_280,   86_711),
}
mispredict_data = {
    'perf':               (      9,      821,    1_228,    1_964,    2_647,    3_548,    4_001,    5_067,    5_578),
    'MCAD':               (     10,     3208,    4_898,    6_542,    8_491,    9_981,   11_845,   13_417,   13_587),
}

def grouped_bar_plot(ax, x_groups, data):
    width = 0.25  # the width of the bars
    multiplier = 0
    xs = [((len(data)+1)*width)*x for x in range(len(x_groups))] # the label locations

    for attribute, measurement in data.items():
        offset = width * multiplier
        rects = ax.bar([x + offset for x in xs], measurement, width, label=attribute)
        #ax.bar_label(rects, padding=3)
        multiplier += 1

    ax.set_xticks([x + (width*(len(data)-1)/2.0) for x in xs], x_groups)

sns.set_theme(palette="terrain")
sns.set_style("whitegrid")

fig, (ax1, ax2) = plt.subplots(2, layout='constrained')

grouped_bar_plot(ax1, x_groups, cycle_data)
ax1.set_ylabel('Cycle count')
ax1.legend(loc='upper left', ncols=3)
ax1.set_xlabel('Proportion of random branches in inner loop')

grouped_bar_plot(ax2, x_groups, mispredict_data)
ax2.set_ylabel('Branch mispredicts')
ax2.set_xlabel('Proportion of random branches in inner loop')
ax2.legend(loc='upper left', ncols=3)

fig.suptitle('Cycle count estimation using branch predictor modeling')

sns.despine()

plt.savefig("plot.png")