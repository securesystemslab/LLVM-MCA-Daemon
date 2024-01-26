# Author - chinmay_dd

import logging
import os
import subprocess
import ipdb
import sys
import grpc

from binaryninja import *

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from . import binja_pb2
from . import binja_pb2_grpc

MCAD_BUILD_PATH = "/home/chinmay_dd/Projects/LLVM-MCA-Daemon/build"
runner = None

def get_triple_and_cpu_info(view):
    if view.arch.name == "x86_64":
        return "x86_64-unknown-linux-gnu", "skylake"

class GRPCClient:
    def __init__(self, view):
        self.view = view
        self.channel = grpc.insecure_channel("localhost:50052")
        self.stub = binja_pb2_grpc.BinjaStub(self.channel)

    def request_cycle_counts(self, instructions):
        b_instructions = [binja_pb2.BinjaInstructions.Instruction(opcode=insn) for insn in instructions]
        return self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instruction=b_instructions))

class MCADRunner:
    def __init__(self, view):
        self.view = view
        self.p = None

        self.triple, self.mcpu = get_triple_and_cpu_info(view)

    def start(self):
        args = []
        args.append(os.path.join(MCAD_BUILD_PATH, "llvm-mcad"))
        args.append("--debug")
        args.append("-mtriple=" + self.triple)
        args.append("-mcpu=" + self.mcpu)
        args.append("-load-broker-plugin=" + os.path.join(MCAD_BUILD_PATH, "plugins", "binja-broker", "libMCADBinjaBroker.so"))
        self.p = subprocess.Popen(args)

    def stop(self):
        if self.p:
            self.p.kill()

def get_cycle_counts(bv):
    instructions = [bytes([0x48, 0x01, 0xD8])]

    client = GRPCClient(bv)
    response = client.request_cycle_counts(instructions)

    print(response)

def start_bridge(bv):
    global runner

    runner = MCADRunner(bv)
    runner.start()

def stop_bridge(bv):
    global runner

    runner.stop()

# Commands
PluginCommand.register("MCAD\\Get Cycle Counts from MCAD", "Retrieve cycle counts for a path using LLVM MCA Daemon", get_cycle_counts)
PluginCommand.register("MCAD\\Start MCAD Bridge", "Starts MCAD Server in the background", start_bridge)
PluginCommand.register("MCAD\\Stop MCAD Bridge", "Stops MCAD Server running in the background", stop_bridge)
