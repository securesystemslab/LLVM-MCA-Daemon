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

class WrappedInstruction:
    def __init__(self, addr=0, length=0, disasm_text=None, block=None, bytez=None, opcode=None):
        self.addr = addr
        self.length = length 
        self.block = block
        self.bytez = bytez
        self.disasm_text = disasm_text

    def get_wrapped_instruction(function, addr):
        i = WrappedInstruction()
        i.addr = addr
        i.length = function.view.get_instruction_length(addr)
        i.block = function.get_basic_block_at(addr)
        i.bytez = function.view.read(addr, i.length)
        i.disasm_text = function.view.get_disassembly(addr)
        return i

class Trace:
    def __init__(self, function, blocks=dict(), instructions=[]):
        self.function = function
        self.view = function.view

        self.blocks = blocks
        self.instructions = instructions

    def add_instruction_at_addr(self, addr):
        self.instructions.append(WrappedInstruction.get_wrapped_instruction(self.function, addr))

    def add_block(self, block):
        self.blocks[block.start] = block

    def get_block_at_addr(self, addr):
        if addr in self.blocks:
            return self.blocks[addr]
        return None

def get_trace(function):
    trace = Trace(function)
    view = function.view
    member_tag = view.get_tag_type("Trace Member")

    current_block = function.get_basic_block_at(function.start)
    explore = True
    visited = set()
    while explore:
        explore = False
        visited.add(current_block.start)
        trace.add_block(current_block)

        for insn in current_block.disassembly_text:
            trace.add_instruction_at_addr(insn.address)

        for edge in current_block.outgoing_edges:
            target = edge.target.start
            # XXX: This does not handle loops yet. We need to identify loop heads so that we can visit them twice.
            # Or, generate a true reverse post-order traversal
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

        # TODO: Warn user if trace ends in a non-exit block?

    return trace

class GRPCClient:
    def __init__(self, view):
        self.view = view
        self.channel = grpc.insecure_channel("localhost:50052")
        self.stub = binja_pb2_grpc.BinjaStub(self.channel)

    def request_cycle_counts(self, instructions):
        b_instructions = [binja_pb2.BinjaInstructions.Instruction(opcode=bytes(insn.bytez)) for insn in instructions]
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
        args.append("--use-call-inst")
        args.append("--use-return-inst")
        args.append("-load-broker-plugin=" + os.path.join(MCAD_BUILD_PATH, "plugins", "binja-broker", "libMCADBinjaBroker.so"))
        self.p = subprocess.Popen(args)

    def stop(self):
        client = GRPCClient(self.view)
        response = client.request_cycle_counts([])

    def is_alive(self):
        if self.p:
            return True
        else:
            return False

def generate_graph(response, trace):
    graph = FlowGraph()
    blocks = dict()
    instructions = dict()

    for block_addr in trace.blocks:
        node = FlowGraphNode(graph)
        blocks[block_addr] = node

    if len(trace.instructions) != len(response.cycle_count):
        logging.error("[MCAD] Incomplete cycle count received from MCAD.")
        return graph

    for idx, insn in enumerate(trace.instructions):
        instructions[insn.addr] = (insn, response.cycle_count[idx])

    for block_addr in trace.blocks:
        lines = []
        block = trace.get_block_at_addr(block_addr)
        for insn in block.disassembly_text:
            cycle_start = instructions[insn.address][1].ready
            cycle_end = instructions[insn.address][1].executed

            tokens = []
            cycle_str = str(cycle_start) + " - " + str(cycle_end)
            tokens.append(InstructionTextToken(InstructionTextTokenType.TextToken, cycle_str))
            tokens.append(InstructionTextToken(InstructionTextTokenType.TextToken, " " * (12 - len(cycle_str))))
            tokens.append(InstructionTextToken(InstructionTextTokenType.AddressDisplayToken, str(hex(insn.address))))
            tokens.append(InstructionTextToken(InstructionTextTokenType.TextToken, "  "))
            for token in insn.tokens:
                if token.type != InstructionTextTokenType.TagToken:
                    tokens.append(token)

            color = None
            if instructions[insn.address][1].is_under_pressure:
                color = HighlightStandardColor.RedHighlightColor

            lines.append(DisassemblyTextLine(tokens, insn.address, color=color))

        blocks[block_addr].lines = lines
        graph.append(blocks[block_addr])

    for block_addr in trace.blocks:
        block = trace.get_block_at_addr(block_addr)
        source = blocks[block.start]
        for outgoing in block.outgoing_edges:
            if outgoing.target.start in blocks:
                dest = blocks[outgoing.target.start]
                edge = EdgeStyle(EdgePenStyle.DashDotDotLine, 2, ThemeColor.AddressColor)
                source.add_outgoing_edge(BranchType.UnconditionalBranch, dest, edge)

    return graph

def get_cycle_counts(view, function):
    global bridge
    
    if not bridge:
        logging.error("[MCAD] Bridge has not been initialized.")
        return

    if not bridge.is_alive():
        logging.error("[MCAD] Server is not alive.")
        return

    trace = get_trace(function)

    client = GRPCClient(view)
    response = client.request_cycle_counts(trace.instructions)

    g = generate_graph(response, trace)
    show_graph_report("MCAD Trace Graph", g)

    del trace
    del response

def initialize(view):
    view.create_tag_type("Trace Member", "🌊")

def start(view):
    global bridge

    bridge = MCADBridge(view)
    bridge.start()

    logging.info("[MCAD] Server started.")

def stop(view):
    global bridge

    bridge.stop()

    logging.info("[MCAD] Server stopped.")

# Commands
PluginCommand.register("MCAD\\Initialize Plugin", "Initialize custom tag for annotations", initialize)
PluginCommand.register("MCAD\\Start Server", "Starts MCAD Server in the background", start)
PluginCommand.register("MCAD\\Stop Server", "Stops MCAD Server running in the background", stop)
PluginCommand.register_for_function("MCAD\\Get Cycle Counts from MCAD", "Retrieve cycle counts for a path using LLVM MCA Daemon", get_cycle_counts)
