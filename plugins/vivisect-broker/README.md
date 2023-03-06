# Vivisect Broker

## Generating Vivisect Server Python Protobuf / gRPC code

```
python3 -m grpc_tools.protoc -I. --python_out=. --pyi_out=. --grpc_python_out=.
```

## Testing the server

In one window, start the server:

```
python3 vivisect_server.py
```

In another window, start the test client

```
python vivisect_test_client.py
```

## Building

Currently only `gRPC` code is generated when built. There is no Broker yet.

```
cmake .
```

```
make
```