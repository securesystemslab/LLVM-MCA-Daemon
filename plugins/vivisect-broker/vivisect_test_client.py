from __future__ import print_function

import logging

import grpc
import vivserver_pb2
import vivserver_pb2_grpc


def run():
    uds_addresses = ['unix:helloworld.sock']
    for uds_address in uds_addresses:
        print(uds_address)
        with grpc.insecure_channel(uds_address) as channel:
            stub = vivserver_pb2_grpc.EmulatorStub(channel)
            response = stub.RunNumInstructions(vivserver_pb2.RunInstructionsRequest(numInstructions=20))
            logging.info('Received: %s', response)


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    run()
