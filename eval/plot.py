#!/usr/bin/env python3
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import argparse

def main(csv_file):
    # Read CSV file; expect columns: Binary, Measurement, cycles, instructions,
    # L1-icache-load-misses, L1-dcache-load-misses, L2-misses, LLC-load-misses
    df = pd.read_csv(csv_file)
    
    # List of normalized metrics to plot
    metrics = ['cycles', 'instructions', 'L1-icache-load-misses', 
               'L1-dcache-load-misses', 'L2-misses', 'LLC-load-misses']
    
    # For each measurement group (e.g. "perf" or "llvm-mcad"), divide each metric
    # by the first data point in that group (i.e. the first binary for that measurement)
    # for metric in metrics:
        # df[metric] = df.groupby('Measurement')[metric].transform(lambda x: x / x.iloc[0])
    
    sns.set_theme(palette="terrain")
    sns.set_style("whitegrid")
    
    for metric in metrics:
        plt.figure(figsize=(8, 3))
        ax = sns.barplot(data=df, x='Binary', y=metric, hue='Measurement')
        # ax.set_title(f'{metric} by access pattern (scaled relative to sequential)')
        ax.set_xlabel('Access Pattern')
        ax.set_ylabel(f'{metric}')
        plt.tight_layout()
        output_filename = f'{metric}_by_binary_scaled.png'
        plt.savefig(output_filename)
        plt.close()
        print(f"Saved plot for {metric} as {output_filename}")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description="Plot bar charts from CSV with binary names on the x-axis, scaling each measurement series relative to the first binary."
    )
    parser.add_argument('--csv', default="benchmark_results.csv", help="Input CSV file name")
    args = parser.parse_args()
    main(args.csv)
