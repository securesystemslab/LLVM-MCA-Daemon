# LLVM MCA Daemon
[LLVM MCA](https://llvm.org/docs/CommandGuide/llvm-mca.html) that performs analysis online.

## Preparations
This project uses a special version of LLVM. Please use the following way to clone it:
```bash
git clone -b dev-incremental-mca git@github.com:securesystemslab/multiarch-llvm-project.git
```
To build it, here are some suggested CMake configurations:
```bash
cd multiarch-llvm-project
mkdir .build && cd .build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
               -DBUILD_SHARED_LIBS=ON \
               -DLLVM_TARGETS_TO_BUILD="X86;ARM" \
               -DLLVM_USE_LINKER=gold \
               ../llvm
ninja llvm-mca llvm-mc LLVMDebugInfoDWARF
```
Note that LLVM-MCAD uses a modular design, so components related to QEMU are no longer built by default. Please checkout their prerequisites inside their folders under the `plugins` directory.

## Build
You only need one additional CMake argument: `LLVM_DIR`. It points to LLVM's CMake folder. Here is an example:
```bash
mkdir .build && cd .build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
               -DLLVM_DIR=/path/to/installed-llvm/lib/cmake/llvm \
               ../
ninja all
```
For instance, if you build `multiarch-llvm-project` using the previous steps, _"/path/to/installed-llvm/lib/cmake/llvm"_ will be _"/path/to/multiarch-llvm-project/.build/lib/cmake/llvm"_.

Note that plugins under the `plugins` folder are not built by default. Please add `-DLLVM_MCAD_BUILD_PLUGINS=ON` if you want to build them. Here are some other CMake arguments you can tweak:
 - `LLVM_MCAD_ENABLE_ASAN`. Enable the address sanitizer.
 - `LLVM_MCAD_ENABLE_TCMALLOC`. Uses tcmalloc and its heap profiler.
 - `LLVM_MCAD_ENABLE_PROFILER`. Uses CPU profiler from [gperftools](https://github.com/gperftools/gperftools).
 - `LLVM_MCAD_FORCE_ENABLE_STATS`. Enable LLVM statistics even in non-debug builds.

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
