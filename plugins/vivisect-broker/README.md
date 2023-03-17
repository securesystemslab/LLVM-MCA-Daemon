# Vivisect Broker

## Generating Vivisect Server Python Protobuf / gRPC code

```
python3 -m grpc_tools.protoc -I. --python_out=. --pyi_out=. --grpc_python_out=.
```

## Testing the server

### Usage

```
$ python3 vivisect_server.py --help
Usage: vivisect_server.py [OPTIONS] BINARY_PATH

Options:
  -a, --architecture [amd64|arm|ppc32-vle|ppc64-server|ppc32-server]
  --address TEXT                  [default: localhost]
  -p, --port TEXT                 [default: 50051]
  -e, --endianness [big|little]
  --help                          Show this message and exit.
```

In another window, start the test client

```
python vivisect_test_client.py
```

or, run with `MCAD`

## `MCAD` Vivisect Broker

Currently only `gRPC` code is generated when built.

```
$ cd .build
$ cmake .
```

```
ninja all
```

### Running `MCAD`

### x86

```
$ .build/llvm-mcad -mtriple="x86_64-unknown-linux-gnu" -mcpu="skylake" --load-broker-plugin=.build/plugins/vivisect-broker/broker/libMCADVivisectBroker.so -broker-plugin-arg-host=127.0.0.1:10090 -debug
```