#!/usr/bin/env python3
import subprocess
import csv
import re
import os
import time
import argparse

# Directories (adjust these paths as needed)
BINARIES_DIR = os.path.expanduser("~/workspace/LLVM-MCA-Daemon/eval/branch_prediction")
SERVER_BUILD_DIR = os.path.expanduser("~/workspace/LLVM-MCA-Daemon/.build")
CLIENT_BUILD_DIR = os.path.expanduser("~/workspace/LLVM-MCA-Daemon/qemu/build")

# Server and client commands
SERVER_CMD = "./llvm-mcad"
CLIENT_CMD = "./qemu-x86_64"

# Perf command and events
PERF_CMD = "/lib/linux-tools/5.15.0-133-generic/perf"
PERF_EVENTS = "cycles,cache-misses,L1-dcache-load-misses,L1-dcache-loads,L1-icache-load-misses,LLC-load-misses,l2_rqsts.miss,instructions,cpu/instructions/"

# CSV header for output rows
CSV_COLUMNS = ["bench", "size", "stride", "cycles", "l1-misses", "l2-misses", "l3-misses"]

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

def generate_binary(size, stride, binary_path):
    """
    Generates an assembly file using gen_cache.py and compiles it.
    The generated assembly file is saved as tmp.S.
    """
    gen_cmd = ["python3", "gen_cache.py", f"--stride={stride}", f"--set_size={size}"]
    print(f"Generating assembly for set_size={size} and stride={stride}...")
    with open("tmp.S", "w") as asm_file:
        subprocess.run(gen_cmd, stdout=asm_file, check=True)
    
    gcc_cmd = ["gcc", "-no-pie", "-nostdlib", "-O0", "-o", binary_path, "tmp.S"]
    print(f"Compiling binary {binary_path}...")
    subprocess.run(gcc_cmd, check=True)
    os.remove("tmp.S")

def map_mcad_stats(stats):
    """
    Maps llvm-mcad output keys to normalized keys.
    We extract:
      - "Total Cycles"      → "cycles"
      - "L1i Load Misses"   → "l1-misses"
      - "L2 Load Misses"     → "l2-misses"
      - "L3 Load Misses"     → "l3-misses"
    """
    mapping = {
        "Total Cycles": "cycles",
        "L1i Load Misses": "l1-misses",
        "L2 Load Misses": "l2-misses",
        "L3 Load Misses": "l3-misses",
    }
    mapped = {}
    for orig_key, new_key in mapping.items():
        if orig_key in stats:
            mapped[new_key] = stats[orig_key]
    return mapped

def map_perf_stats(stats):
    """
    Maps perf output keys to normalized keys.
    We extract:
      - "cycles"                      → "cycles"
      - "L1-dcache-load-misses"       → "l1-misses"
      - "l2_rqsts.miss"               → "l2-misses"
      - "LLC-load-misses"              → "l3-misses"
    """
    mapping = {
        "cycles": "cycles",
        "L1-dcache-load-misses": "l1-misses",
        "l2_rqsts.miss": "l2-misses",
        "LLC-load-misses": "l3-misses",
    }
    mapped = {}
    for orig_key, new_key in mapping.items():
        if orig_key in stats:
            mapped[new_key] = stats[orig_key]
    return mapped

def parse_stats(output):
    """
    Parses key-value pairs from the llvm-mcad output.
    Expected format: "Key:    Value"
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

def parse_perf_output(output):
    """
    Parses the perf output into a dictionary.
    """
    stats = {}
    event_pattern = re.compile(r'^\s*(?P<value>[\d,]+|<not counted>)\s+(?P<event>[^:]+):')
    time_pattern = re.compile(r'^\s*(?P<value>[\d\.]+)\s+seconds\s+(?P<type>time elapsed|user|sys)')
    for line in output.splitlines():
        line = line.strip()
        if not line or line.startswith("Performance counter stats"):
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
    return stats

def run_benchmark(binary, port, size, stride):
    """
    Runs the llvm-mcad benchmark for the given binary on the specified port.
    Returns normalized stats.
    """
    print(f"\nRunning llvm-mcad benchmark for binary: {binary} on port {port}")
    server_args = [
        '-mtriple=x86_64-unknown-linux-gnu',
        '-mcpu=skylake',
        f"--load-broker-plugin={os.path.join(SERVER_BUILD_DIR, 'plugins', 'qemu-broker', 'libMCADQemuBroker.so')}",
        f'-broker-plugin-arg-host=localhost:{port}',
        '--enable-cache=L1i',
        '--enable-cache=L1d',
        '--enable-cache=L2',
        '--enable-cache=L3',
        "--l1-latency=1",
        "--l2-latency=100",
        "--l3-latency=300",
        "--memory-latency=3000",
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
    time.sleep(1)  # Allow server to start
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
    raw_stats = parse_stats(full_output)
    norm_stats = map_mcad_stats(raw_stats)
    norm_stats["bench"] = "mcad"
    norm_stats["size"] = size
    norm_stats["stride"] = stride
    return norm_stats

def run_perf(binary, port, size, stride):
    """
    Runs perf on the given binary and returns normalized stats.
    """
    binary_path = os.path.join(BINARIES_DIR, binary)
    perf_cmd = [
        PERF_CMD, "stat",
        "-e", PERF_EVENTS,
        binary_path
    ]
    print("Running perf:", " ".join(perf_cmd))
    result = subprocess.run(perf_cmd, cwd=BINARIES_DIR, capture_output=True, text=True)
    perf_output = result.stderr
    raw_stats = parse_perf_output(perf_output)
    norm_stats = map_perf_stats(raw_stats)
    norm_stats["bench"] = "perf"
    norm_stats["size"] = size
    norm_stats["stride"] = stride
    return norm_stats

def write_csv(results, filename="benchmark_results.csv"):
    """Writes the results to a CSV file with header: bench, size, stride, cycles, l1-misses, l2-misses, l3-misses."""
    with open(filename, "w", newline="") as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=CSV_COLUMNS)
        writer.writeheader()
        for result in results:
            row = {key: result.get(key, "") for key in CSV_COLUMNS}
            writer.writerow(row)
    print(f"\nBenchmark results saved to {filename}")

def main(output_file, port, sizes_str, strides_str):
    results = []
    build_server()

    # If the user specifies "default" for both sizes and strides, use the predefined combo.
    if sizes_str.lower() == "default" and strides_str.lower() == "default":
        default_combos = [
            (128, 1), (256, 1), (512, 1), (1024, 1), (2048, 1), (4096, 1), (8192, 1)
        ] + [(128, s) for s in [2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 24]]
        combos = default_combos
    else:
        # Otherwise, parse the comma-separated lists and form the Cartesian product.
        sizes = [int(x.strip()) for x in sizes_str.split(',')]
        strides = [int(x.strip()) for x in strides_str.split(',')]
        combos = [(s, r) for s in sizes for r in strides]

    for size, stride in combos:
        binary_name = f"binary_{size}_{stride}"
        binary_path = os.path.join(BINARIES_DIR, binary_name)
        generate_binary(size, stride, binary_path)
        mcad_stats = run_benchmark(binary_name, port, size, stride)
        print("Parsed llvm-mcad stats:", mcad_stats)
        results.append(mcad_stats)
        perf_stats = run_perf(binary_name, port, size, stride)
        print("Parsed perf stats:", perf_stats)
        results.append(perf_stats)
    write_csv(results, filename=output_file)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description="Generate cache binary with given stride and set size, run benchmarks with llvm-mcad and perf, and output CSV in the format:\n"
                    "bench, size, stride, l1-misses, l2-misses, l3-misses, cycles"
    )
    parser.add_argument("--output", "-o", default="benchmark_results.csv", help="CSV output file name.")
    parser.add_argument("--port", "-p", default=9488, type=int, help="Port number to use for both server and client.")
    parser.add_argument("--sizes", "-s", default="default",
                        help="Comma-separated list of set sizes (e.g., 128,256,512,1024) or 'default' to use preset combinations")
    parser.add_argument("--strides", "-r", default="default",
                        help="Comma-separated list of strides (e.g., 1,2,3,4) or 'default' to use preset combinations")
    args = parser.parse_args()
    
    main(output_file=args.output, port=args.port, sizes_str=args.sizes, strides_str=args.strides)
