# LLVM MCA Daemon
[LLVM MCA](https://llvm.org/docs/CommandGuide/llvm-mca.html) that performs analysis online.

## Preparations
On Ubuntu 22.04, you will need at the very least before you get started:
```
sudo apt install build-essential binutils cmake ninja-build
```
This project uses a special version of LLVM that builds on top of LLVM 13.0.0. Please use the following way to clone it:
```bash
git clone -b dev-incremental-mca https://github.com/securesystemslab/llvm-project.git
```
To build and install it, here are some suggested CMake configurations:
```bash
cd llvm-project
mkdir install
mkdir .build && cd .build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
               -DCMAKE_INSTALL_PREFIX=$(realpath ../install) \
               -DBUILD_SHARED_LIBS=ON \
               -DLLVM_TARGETS_TO_BUILD="X86;ARM;PowerPC" \
               -DLLVM_USE_LINKER=gold \
               -DLLVM_DEFAULT_TARGET_TRIPLE=x86_64-pc-linux-gnu \
               ../llvm
ninja llvm-mca llvm-mc LLVMDebugInfoDWARF
ninja install
```
Note that LLVM-MCAD uses a modular design, so components related to QEMU are no longer built by default. Please checkout their prerequisites inside their folders under the `plugins` directory.

## Build
You only need one additional CMake argument: `LLVM_DIR`. This should point to LLVM's CMake subdirectory inside of the install prefix you gave above as `CMAKE_INSTALL_PREFIX`. Here is an example:
```bash
mkdir .build && cd .build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
               -DLLVM_DIR=$(realpath ../install)/lib/cmake/llvm \
               ../
ninja all
```
For instance, if you build `llvm-project` using the previous steps, _"/path/to/installed-llvm/lib/cmake/llvm"_ will be _"/path/to/llvm-project/.build/lib/cmake/llvm"_.

Note that plugins under the `plugins` folder are not built by default. Please add `-DLLVM_MCAD_ENABLE_PLUGINS=all` if you want to build all of them, or give a semicolon-separated list of the choices `qemu`, `tracer`, `binja` or `vivisect` to select specifically which plugins to build. 

Here are some other CMake arguments you can tweak:
 - `LLVM_MCAD_ENABLE_ASAN`. Enable the address sanitizer.
 - `LLVM_MCAD_ENABLE_TCMALLOC`. Uses tcmalloc and its heap profiler.
 - `LLVM_MCAD_ENABLE_PROFILER`. Uses CPU profiler from [gperftools](https://github.com/gperftools/gperftools).
 - `LLVM_MCAD_FORCE_ENABLE_STATS`. Enable LLVM statistics even in non-debug builds.

## Docker

We also ship LLVM-MCAD with Docker. Simply run `./up` from the docker directory. Then use MCAD as so:  

```bash
$ # LLVM-MCA-Daemon directory is located inside /work
$ # Port mappings are different for different Brokers. 50051 - Vivisect, 50052 - Binja
$ docker run -p 50052:50052 mcad_dev --debug -mtriple="armv7-linux-gnueabihf" -mcpu="cortex-a57" --use-call-inst --use-return-inst --noalias=false -load-broker-plugin=/work/LLVM-MCA-Daemon/build/plugins/binja-broker/libMCADBinjaBroker.so
```

## Usages
Here is an example of using `llvm-mcad` -- the main command line tool -- with the qemu-broker Broker plugin (Please refer to the `plugins/qemu-broker` folder for more details about how to build this plugin).

First, on the server side:
```bash
# Server
cd .build
# ARM
./llvm-mcad -mtriple="armv7-linux-gnueabihf" -mcpu="cortex-a57" \
            --load-broker-plugin=$PWD/plugins/qemu-broker/libMCADQemuBroker.so \
            -broker-plugin-arg-host="localhost:9487"
# X86
./llvm-mcad -mtriple="x86_64-unknown-linux-gnu" -mcpu="skylake" \
            --load-broker-plugin=$PWD/plugins/qemu-broker/libMCADQemuBroker.so \
            -broker-plugin-arg-host="localhost:9487"

# PowerPC 64-bit little endian
./llvm-mcad -mtriple="powerpcle-linux-gnu" -mcpu="pwr10" \
    --load-broker-plugin=$PWD/plugins/qemu-broker/libMCADQemuBroker.so \
    -broker-plugin-arg-host="localhost:9487"
```

Then, on the client side:
```bash
# Client
# ARM
/path/to/qemu/build/qemu-arm -L /usr/arm-linux-gnueabihf \
      -plugin /path/to/llvm-mcad/.build/plugins/qemu-broker/Qemu/libQemuRelay.so,\
      arg="-addr=127.0.0.1",arg="-port=9487" \
      -d plugin ./hello_world.arm
# X86
/path/to/qemu/build/qemu-x86_64 \
      -plugin /path/to/llvm-mcad/.build/plugins/qemu-broker/Qemu/libQemuRelay.so,\
      arg="-addr=127.0.0.1",arg="-port=9487" \
      -d plugin ./hello_world.x86_64
# PowerPC 64-bit little endian
/path/to/qemu/build/qemu-ppc64le \
      -plugin /path/to/llvm-mcad/.build/plugins/qemu-broker/Qemu/libQemuRelay.so,\
      arg="-addr=127.0.0.1",arg="-port=9487" \
      -d plugin ./hello_world.ppc64le
```

Here are some other important command line arguments:
 - `-mca-output=<file>`. Print the MCA analysis result to a file instead of STDOUT.
 - `-broker=<asm|raw|plugin>`. Select the Broker to use.
   - **asm**. Uses the `AsmFileBroker`, which reads the input from an assembly file. This broker essentially turns the tool into the original `llvm-mca`.
   - **raw**. Uses the `RawBytesBroker`. Functionality TBD.
   - **plugin**. Uses Broker plugin loaded by the `-load-broker-plugin` flag.
 - `-load-broker-plugin=<plugin library file>`. Load a Broker plugin. This option implicitly selects the **plugin** Broker kind.
 - `-broker-plugin-arg.*`. Supply addition arguments to the Broker plugin. For example, if `-broker-plugin-arg-foo=bar` is given, the plugin will receive `-foo=bar` argument when it's registering with the core component.
 - `-cache-sim-config=<config file>`. Please refer to [this document](doc/cache-simulation.md) for more details.

## Design
### Overview
LLVM-MCAD is roughly splitted into two parts: The core component and the Broker. The core component manages and runs LLVM MCA. It is using a modified version of MCA which analyzes input instructions _incrementally_. That is, instead of retrieving all native instructions (from an assembly file, for example) and analyzing them at once, incremental MCA continuously fetches small batch of instructions and analyze them before fetching the next batch. The "native instructions" we're discussing here -- the input to MCA -- are represented by `llvm::MCInst` instances. And Broker is the one who supplies these `MCInst` batches to the core component.

A Broker exposes a simple interface, defined by the `Broker` class, to the core component. You can either choose from one of the two built-in Brokers -- `AsmFileBroker` and `RawBytesBroker` (WIP) -- or create your own Brokers via the Broker plugin. There is basically no assumption on the execution model of a Broker, so you can either create a simple Broker like `AsmFileBroker`, which simply reads from an assembly file, or a complex one like `qemu-broker` in the `plugins` folder that interfaces with QEMU using TCP socket and multi-threading.

Currently we're only displaying the MCA result using `SummaryView`, which print nothing but basic information like total cycle counts and number of uOps. In the future we're planning to support more variety of MCA views, or even another plugin system for customized views.

### Goals
 - **LLVM MCA for dynamic analysis**. A lightweight, general-purpose execution trace simulation and analysis framework.
 - Able to perform online analysis whose target program runs for a long duration.
 - Able to scale up with the input.
   - Acceptable (analysis) performance
   - Low memory footprint
 - Good interoperatability with upstream LLVM
   - Be able to upstream this project (the core component) in the future.

### Non-Goals
 - Fixed number of input sources.
   - Different from the original `llvm-mca` tool, you can create your own Broker plugin to harvest `MCInst` from arbitrary medias like execution traces or object files.
 - Multi-threading in the core component
   - The core component should have a simple execution model, so we don't run MCA on a separate thread. You can, and _encouraged_ to, run your custom Broker on a separate thread to increase the throughput.
 - Has any assumption on Broker plugin's execution model.
 - Manage Broker plugin's lifecycle.
   - We dont' have explicit callbacks for Broker plugin's lifecycle. Developers of Brokers are expected to manage the lifecycle on their own, and encouraged to execute tasks in an on-demand fashion (e.g. `AsmFileBroker` only parses the assembly file after the first invocation to its `fetch` method).

# Acknowledgements
This material is based upon work supported by the Defense Advanced Research Projects Agency (DARPA) and Naval Information Warfare Center Pacific (NIWC Pacific) under Contract Number N66001-20-C-4027 and 140D0423C0063. Any opinions, findings and conclusions or recommendations expressed in this material are those of the author(s) and do not necessarily reflect the views of the DARPA, NIWC Pacific, or its Contracting Agent, the U.S. Department of the Interior, Interior Business Center, Acquisition Services Directorate, Division III.
