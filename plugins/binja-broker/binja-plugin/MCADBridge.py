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
bridge = None

def get_triple_and_cpu_info(view):
    if view.arch.name == "x86_64":
        return "x86_64-unknown-linux-gnu", "skylake"

def get_instruction_trace_from_function(function):
    instructions = []
    view = function.view
    member_tag = view.get_tag_type("Trace Member")

    current_block = function.get_basic_block_at(function.start)
    explore = True
    visited = set()
    while explore:
        explore = False
        visited.add(current_block.start)

        for insn in current_block.disassembly_text:
            addr = insn.address
            length = view.get_instruction_length(addr)
            instructions.append(view.read(addr, length))

        for edge in current_block.outgoing_edges:
            target = edge.target.start
            if target in visited:
                continue

            tags_at_addr = function.get_tags_at(target)
            found = False
            for tag in tags_at_addr:
                if tag.type == member_tag:
                    found = True
                    break
            
            if found:
                explore = True
                current_block = edge.target
                break

    return instructions

class GRPCClient:
    def __init__(self, view):
        self.view = view
        self.channel = grpc.insecure_channel("localhost:50052")
        self.stub = binja_pb2_grpc.BinjaStub(self.channel)

    def request_cycle_counts(self, instructions):
        b_instructions = [binja_pb2.BinjaInstructions.Instruction(opcode=bytes(insn)) for insn in instructions]
        print(b_instructions)
        return self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instruction=b_instructions))

class MCADBridge:
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

    def is_alive(self):
        if self.p:
            return True
        else:
            return False

def get_cycle_counts(view, function):
    global bridge
    
    if not bridge:
        logging.error("[MCAD] Bridge has not been initialized.")
        return

    if not bridge.is_alive():
        logging.error("[MCAD] Server is not alive.")
        return

    instructions = get_instruction_trace_from_function(function)
    print(instructions)

    client = GRPCClient(view)
    response = client.request_cycle_counts(instructions)

def initialize(view):
    view.create_tag_type("Trace Member", "🌊")

def start(view):
    global bridge

    bridge = MCADBridge(view)
    bridge.start()

    logging.info("[MCAD] Server started.")

def cleanup(view):
    global bridge

    bridge.stop()

    logging.info("[MCAD] Server stopped.")

# Commands
PluginCommand.register("MCAD\\Initialize Plugin", "Initialize custom tag for annotations", initialize)
PluginCommand.register("MCAD\\Start Server", "Starts MCAD Server in the background", start)
PluginCommand.register("MCAD\\Cleanup Server", "Stops MCAD Server running in the background", cleanup)
PluginCommand.register_for_function("MCAD\\Get Cycle Counts from MCAD", "Retrieve cycle counts for a path using LLVM MCA Daemon", get_cycle_counts)
