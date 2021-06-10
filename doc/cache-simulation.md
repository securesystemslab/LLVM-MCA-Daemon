# Cache Simulation
Our modified version of MCA allows users to simulate cache accesses and take into consideration of their latencies.

## Usage
To use this feature, please supply the path to cache configuration file via the `-cache-sim-config=<file>` command line option. For example:
```bash
llvm-mcad ... -mcpu=skylake -cache-sim-config=skylake.cache-config.json ...
```
The following section explains the format of the configuration file.

## Config file format
The configuration file is written in JSON. The top level should always be an object, which encloses fields representing a specific level of cache. For example, the following JSON presents the config for a L1 data cache:
```json
{
    "l1d": {
        "size": 4096,
        "associate": 1,
        "line_size": 64
    }
}
```
Currently we only support "l1d" and "l2d" keys for L1D and L2D caches, respectively.

Each cache level entry can have the following properties:
 - `size`. Total size of this cache.
 - `associate`. Number of cache association.
 - `line_size`. Size of a cache line.
 - `penalty`. Number of penalty cycles if a cache miss happens.

All of these fields are optional.

## Note
LLVM _can_ provide cache configuration for each CPU (via `TargetTransformInfo`). However, in order to retrieve these number we need to import libraries for backend targets and it's not trivial to get a `TargetTransformInfo` instance either. What's worse, many mainstream targets (e.g. ARM) don't provide those information. Therefore, I found it easier to just use configuration file.