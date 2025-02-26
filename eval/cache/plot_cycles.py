#!/usr/bin/env python3

import matplotlib.pyplot as plt
import seaborn as sns
import csv

# the following data was obtained with:
# - vary stride and active_set_size in gen_cache.py before compiling

# repository commit: 

# baseline (CPU/perf):
# pushd LLVM-MCA-Daemon/eval/cache/; make; popd; /usr/lib/linux-tools-5.15.0-97/perf stat -e "cache-misses,L1-dcache-load-miss,l2_rqsts.miss,offcore_requests.all_requests" ./LLVM-MCA-Daemon/eval/cache/x

# MCAD command: 
# ./LLVM-MCA-Daemon/build/llvm-mcad -mtriple="x86_64-unknown-linux-gnu" -mcpu="skylake" --load-broker-plugin=./LLVM-MCA-Daemon/build/plugins/qemu-broker/libMCADQemuBroker.so -broker-plugin-arg-host="localhost:9487" --enable-cache=L1d --enable-cache=L2 --enable-cache=L3cat

# QEMU command:
# ./qemu/build/qemu-x86_64 -plugin ./LLVM-MCA-Daemon/build/plugins/qemu-broker/Qemu/libQemuRelay.so,arg="-addr=127.0.01",arg="-port=9487" -d plugin ./LLVM-MCA-Daemon/eval/cache/x

data = {'perf':{}, 'mcad':{}} # map: bench -> (size -> (stride -> (cycles, l1-misses, l2-misses, l3-misses)))
with open('benchmark_results.csv') as csvfile:
    reader = csv.reader(csvfile)
    next(reader) # skip header
    for bench, size, stride, cycles, l1m, l2m, l3m in reader:
        bench=bench.strip()
        size=int(size)
        stride=int(stride)
        cycles = int(cycles)
        l1m=int(l1m)
        l2m=int(l2m or '0')
        l3m=int(l3m or '0')
        if size not in data[bench]:
            data[bench][size] = {}
        data[bench][size][stride] = (cycles, l2m, l3m)


def grouped_bar_plot(ax, x_groups, data):
    assert all(len(d) == len(x_groups) for d in data.values())
    width = 0.25  # the width of the bars
    multiplier = 0
    xs = [((len(data)+1)*width)*x for x in range(len(x_groups))] # the label locations

    for attribute, measurement in data.items():
        offset = width * multiplier
        rects = ax.bar([x + offset for x in xs], measurement, width, label=attribute)
        #ax.bar_label(rects, padding=3)
        multiplier += 1

    ax.set_xticks([x + (width*(len(data)-1)/2.0) for x in xs], x_groups)


x_group_sizes = (128, 256, 512, 1024, 2048, 4096) #, 8192)
size_misses = {
    'perf Relative Cycles': [data['perf'][size][1][0] / data['perf'][x_group_sizes[0]][1][0] for size in x_group_sizes],
    'MCAD Relative Cycles': [data['mcad'][size][1][0] / data['mcad'][x_group_sizes[0]][1][0] for size in x_group_sizes]
}

x_group_strides = (1, 2, 3, 4, 5, 6, 7, 8, 9, 10,  11, 12, 13, 14, 15, 16) #, 24)
stride_misses = {
    'perf Relative Cycles': [data['perf'][128][stride][0] / data['perf'][128][x_group_strides[0]][0] for stride in x_group_strides],
    'MCAD Relative Cycles': [data['mcad'][128][stride][0] / data['mcad'][128][x_group_strides[0]][0] for stride in x_group_strides]
}

sns.set_theme(palette="terrain")
sns.set_style("whitegrid")

fig, (ax1, ax2) = plt.subplots(2, layout='constrained')

grouped_bar_plot(ax1, list(map(str, x_group_sizes)), size_misses)
ax1.set_ylabel('Relative Cycles')
ax1.legend(loc='upper left', ncols=3)
ax1.set_xlabel('Active Set Size (# cache lines)')
# ax1.set_ylim([0, 13_000])

grouped_bar_plot(ax2, list(map(str, x_group_strides)), stride_misses)
ax2.set_ylabel('Relative Cycles')
ax2.set_xlabel('Stride Between Accessed Elements (# cache lines)')
ax2.legend(loc='upper left', ncols=3)
# ax2.set_ylim([0, 13_000])

fig.suptitle('Cache Simulation')

sns.despine()

plt.savefig("plot.png")