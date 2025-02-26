#!/usr/bin/env python3
import subprocess
import csv
import re
import os
import time
import argparse

# List of binaries to test (you can add more names here)
BINARIES = ["sequential", "random"]

# Directories (adjust these paths to match your setup)
BINARIES_DIR = os.path.expanduser("~/workspace/LLVM-MCA-Daemon/eval/branch_prediction")
SERVER_BUILD_DIR = os.path.expanduser("~/workspace/LLVM-MCA-Daemon/.build")
CLIENT_BUILD_DIR = os.path.expanduser("~/workspace/LLVM-MCA-Daemon/qemu/build")

# Server binary command (arguments are built dynamically based on the port)
SERVER_CMD = "./llvm-mcad"

# Client binary command (arguments are built dynamically based on the port)
CLIENT_CMD = "./qemu-x86_64"

# Perf command (adjust path if needed)
PERF_CMD = "/lib/linux-tools/5.15.0-133-generic/perf"
# Perf events to measure
PERF_EVENTS = "cycles,cache-misses,L1-dcache-load-misses,L1-dcache-loads,L1-icache-load-misses,LLC-load-misses,l2_rqsts.miss,instructions,cpu/instructions/"

# The normalized metrics (for both CSV layouts)
NORMALIZED_METRICS = [
    "cycles",
    "instructions",
    "L1-icache-load-misses",
    "L1-dcache-load-misses",
    "L2-misses",
    "LLC-load-misses",
]

# When writing non-transposed CSV, these columns (plus Binary and Measurement) are used.
MAPPED_COLUMNS = ["Binary", "Measurement"] + NORMALIZED_METRICS

def build_server():
    """Builds the server using ninja (only once)."""
    print("Building server with ninja...")
    try:
        subprocess.run(["ninja"], cwd=SERVER_BUILD_DIR, check=True)
    except subprocess.CalledProcessError as e:
        print("Failed to build the server:", e)
        exit(1)

def wait_for_server_ready(server_proc, port):
    """Wait until the server prints the readiness message with the given port."""
    ready_output = []
    ready_str = f"Listening on localhost:{port}"
    while True:
        line = server_proc.stdout.readline()
        if line == '':
            break  # EOF reached
        print(line, end='')  # Optionally echo the server output
        ready_output.append(line)
        if ready_str in line:
            break
    return "".join(ready_output)

def map_mcad_stats(stats):
    """Map MCAD stats keys to normalized metric names and drop unmapped keys."""
    mapping = {
        "Total Cycles": "cycles",
        "Instructions": "instructions",
        "L1i Load Misses": "L1-icache-load-misses",
        "L1d Load Misses": "L1-dcache-load-misses",
        "L2 Load Misses": "L2-misses",
        "L3 Load Misses": "LLC-load-misses",
    }
    mapped = {}
    for orig_key, new_key in mapping.items():
        if orig_key in stats:
            mapped[new_key] = stats[orig_key]
    return mapped

def map_perf_stats(stats):
    """Map perf stats keys to normalized metric names and drop unmapped keys."""
    mapping = {
        "cycles": "cycles",
        "instructions": "instructions",
        "L1-dcache-load-misses": "L1-dcache-load-misses",
        "L1-icache-load-misses": "L1-icache-load-misses",
        "l2_rqsts.miss": "L2-misses",
        "LLC-load-misses": "LLC-load-misses",
    }
    mapped = {}
    for orig_key, new_key in mapping.items():
        if orig_key in stats:
            mapped[new_key] = stats[orig_key]
    return mapped

def run_benchmark(binary, port):
    """Runs the llvm-mcad benchmark for a given binary and port.
    
    Launches the server and client, waits for completion, and returns normalized stats.
    """
    print(f"\nRunning llvm-mcad benchmark for binary: {binary} on port {port}")
    server_args = [
        '-mtriple=x86_64-unknown-linux-gnu',
        '-mcpu=skylake',
        f"--load-broker-plugin={os.path.join(SERVER_BUILD_DIR, 'plugins', 'qemu-broker', 'libMCADQemuBroker.so')}",
        f'-broker-plugin-arg-host=localhost:{port}',
        # '--enable-cache=L1i',
        '--enable-cache=L1d',
        '--enable-cache=L2',
        '--enable-cache=L3',
        "--l1-latency=1",
        "--l2-latency=100",
        "--l3-latency=300",
        "--memory-latency=20000",
    ]
    client_args = [
        "-plugin",
        f"../../.build/plugins/qemu-broker/Qemu/libQemuRelay.so,arg=-addr=127.0.0.1,arg=-port={port}",
        "-d",
        "plugin"
    ]
    
    server_proc = subprocess.Popen(
        [SERVER_CMD] + server_args,
        cwd=SERVER_BUILD_DIR,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    
    # Optionally wait for readiness; here we sleep briefly.
    time.sleep(1)
    initial_output = ""
    
    binary_path = os.path.join(BINARIES_DIR, binary)
    client_cmd = [CLIENT_CMD] + client_args + [binary_path]
    print("Launching client:", " ".join(client_cmd))
    
    client_proc = subprocess.Popen(
        client_cmd,
        cwd=CLIENT_BUILD_DIR,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    
    client_stdout, client_stderr = client_proc.communicate()
    if client_proc.returncode != 0:
        print(f"Client process failed (binary: {binary}) with error:\n{client_stderr}")
    
    remaining_stdout, remaining_stderr = server_proc.communicate()
    full_output = initial_output + remaining_stdout
    if server_proc.returncode != 0:
        print(f"Server process failed (binary: {binary}) with error:\n{remaining_stderr}")
    
    stats = parse_stats(full_output)
    stats = map_mcad_stats(stats)
    stats["Binary"] = binary
    stats["Measurement"] = "llvm-mcad"
    return stats

def parse_stats(output):
    """
    Parses key-value pairs from the llvm-mcad output.
    
    Expected format: "Key:    Value".
    """
    stats = {}
    pattern = re.compile(r'^([^:]+):\s+(.*)$')
    for line in output.splitlines():
        line = line.strip()
        match = pattern.match(line)
        if match:
            key = match.group(1).strip()
            value = match.group(2).strip()
            try:
                if '.' in value:
                    value = float(value)
                else:
                    value = int(value)
            except ValueError:
                pass
            stats[key] = value
    return stats

def run_perf(binary):
    """Runs perf on the given binary and returns normalized stats."""
    binary_path = os.path.join(BINARIES_DIR, binary)
    perf_cmd = [
        PERF_CMD, "stat",
        "-e", PERF_EVENTS,
        binary_path
    ]
    print("Running perf:", " ".join(perf_cmd))
    result = subprocess.run(perf_cmd, cwd=BINARIES_DIR, capture_output=True, text=True)
    perf_output = result.stderr
    perf_stats = parse_perf_output(perf_output)
    perf_stats = map_perf_stats(perf_stats)
    perf_stats["Binary"] = binary
    perf_stats["Measurement"] = "perf"
    return perf_stats

def parse_perf_output(output):
    """
    Parses the perf output into a dictionary.
    """
    stats = {}
    event_pattern = re.compile(r'^\s*(?P<value>[\d,]+|<not counted>)\s+(?P<event>[^:]+):')
    time_pattern = re.compile(r'^\s*(?P<value>[\d\.]+)\s+seconds\s+(?P<type>time elapsed|user|sys)')
    
    for line in output.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("Performance counter stats"):
            continue
        m = event_pattern.match(line)
        if m:
            raw_value = m.group("value")
            event = m.group("event").strip().replace(":u", "")
            if raw_value == "<not counted>":
                value = None
            else:
                value = int(raw_value.replace(",", ""))
            stats[event] = value
            continue
        m = time_pattern.match(line)
        if m:
            value = float(m.group("value"))
            time_type = m.group("type").strip()
            stats[time_type] = value
            continue
    return stats

def write_csv(results, filename="benchmark_results.csv"):
    """Writes the mapped columns (one measurement per row) into a CSV file."""
    with open(filename, "w", newline="") as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=MAPPED_COLUMNS)
        writer.writeheader()
        for result in results:
            row = {key: result.get(key, "") for key in MAPPED_COLUMNS}
            writer.writerow(row)
    print(f"\nBenchmark results saved to {filename}")

def write_transposed_csv(results, filename="benchmark_results.csv"):
    """
    Writes a transposed CSV:
      - First column "Metric".
      - Each subsequent column is labeled "Binary (Measurement)".
      - Each row corresponds to one normalized metric.
    """
    # Build column headers for measurements.
    col_headers = [f"{r['Binary']} ({r['Measurement']})" for r in results]
    
    rows = []
    for metric in NORMALIZED_METRICS:
        row = {"Metric": metric}
        for i, result in enumerate(results):
            row[col_headers[i]] = result.get(metric, "")
        rows.append(row)
    
    header = ["Metric"] + col_headers
    with open(filename, "w", newline="") as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=header)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)
    print(f"\nBenchmark results saved to {filename}")

def main(output_file, port, transpose):
    results = []
    build_server()
    for binary in BINARIES:
        mcad_stats = run_benchmark(binary, port)
        print("Parsed llvm-mcad stats:", mcad_stats)
        results.append(mcad_stats)
        
        perf_stats = run_perf(binary)
        print("Parsed perf stats:", perf_stats)
        results.append(perf_stats)
    
    if transpose:
        write_transposed_csv(results, filename=output_file)
    else:
        write_csv(results, filename=output_file)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description="Run benchmarks and collect normalized llvm-mcad and perf stats. "
                    "Use --transpose to swap rows and columns in the output CSV."
    )
    parser.add_argument("--output", "-o", default="benchmark_results.csv", help="CSV output file name.")
    parser.add_argument("--port", "-p", default=9488, type=int, help="Port number to use for both server and client.")
    parser.add_argument("--transpose", "-t", default=False, action="store_true", help="Transpose the CSV output (metrics as rows).")
    args = parser.parse_args()
    main(output_file=args.output, port=args.port, transpose=args.transpose)
