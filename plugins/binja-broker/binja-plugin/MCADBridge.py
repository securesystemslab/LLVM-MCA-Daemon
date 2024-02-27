# Author - chinmay_dd

import logging
import os
import subprocess
import sys
from binaryninja import *

sys.path.append(os.path.dirname(os.path.abspath(__file__)))

from grpcclient import GRPCClient
from infoctx import get_info_ctx_for_function, get_info_ctx_for_annotated

MCAD_BUILD_PATH = "/home/chinmay_dd/Projects/LLVM-MCA-Daemon/build"
bridge = None

# Define aliases
ITT = InstructionTextToken
ITTType = InstructionTextTokenType

def get_triple_and_cpu_info(view):
    if view.arch.name == "x86_64":
        return "x86_64-unknown-linux-gnu", "skylake"

    elif view.arch.name == "thumb2":
        return "arm-none-linux-gnueabi", "cortex-a17"

    elif view.arch.name == "aarch64":
        return "aarch64-unknown-linux-gnu", "cortex-a55"

    else:
        logging.error("[MCAD] Unknown architecture. Please update MCADBridge")
        sys.exit(0)

class MCADBridge:
    def __init__(self, view):
        self.view = view
        self.p = None

        self.triple, self.mcpu = get_triple_and_cpu_info(view)

    def start(self):
        if self.is_alive():
            return

        args = []
        args.append(os.path.join(MCAD_BUILD_PATH, "llvm-mcad"))
        args.append("--debug")
        args.append("-mtriple=" + self.triple)
        args.append("-mcpu=" + self.mcpu)
        args.append("--use-call-inst")
        args.append("--use-return-inst")
        args.append("--noalias=false")
        args.append("-load-broker-plugin=" + os.path.join(MCAD_BUILD_PATH, "plugins", "binja-broker", "libMCADBinjaBroker.so"))
        self.p = subprocess.Popen(args)

    def stop(self):
        client = GRPCClient()
        client.request_cycle_counts(None)

        self.p = None

    def is_alive(self):
        return bool(self.p)

#####################################
def generate_graph(response, info_ctx):
    graph = FlowGraph()
    g_blocks = dict()
    instructions = dict()

    idx = 0
    for block_addr in info_ctx.block_list:
        node = FlowGraphNode(graph)
        g_blocks[block_addr] = node

        block = info_ctx.get_block_at_addr(block_addr)
        block_total_cycles = 0
        lines = []

        for insn in block.disassembly_text:
            cycle_start = response.cycle_count[idx].ready
            cycle_end = response.cycle_count[idx].executed

            num_cycles = cycle_end - cycle_start
            block_total_cycles += num_cycles
            cycle_str = str(num_cycles)

            tokens = []
            tokens.append(ITT(ITTType.TextToken, cycle_str))
            tokens.append(ITT(ITTType.TextToken, " " * (6 - len(cycle_str))))
            tokens.append(ITT(ITTType.AddressDisplayToken, str(hex(insn.address))))
            tokens.append(ITT(ITTType.TextToken, "  "))
            for token in insn.tokens:
                if token.type != ITTType.TagToken:
                    tokens.append(token)

            color = None
            if response.cycle_count[idx].is_under_pressure:
                color = HighlightStandardColor.RedHighlightColor
            elif num_cycles > 20:
                color = HighlightStandardColor.OrangeHighlightColor

            lines.append(DisassemblyTextLine(tokens, insn.address, color=color))
            idx += 1

        total_string = "~   " + str(block_total_cycles) + "   ~"
        lines.append(DisassemblyTextLine([ITT(ITTType.TextToken, total_string)]))
        g_blocks[block_addr].lines = lines
        graph.append(g_blocks[block_addr])

    for block_addr in info_ctx.blocks:
        block = info_ctx.get_block_at_addr(block_addr)
        source = g_blocks[block.start]
        for outgoing in block.outgoing_edges:
            if outgoing.target.start in g_blocks:
                dest = g_blocks[outgoing.target.start]
                edge = EdgeStyle(EdgePenStyle.DashDotDotLine, 2, ThemeColor.AddressColor)
                source.add_outgoing_edge(BranchType.UnconditionalBranch, dest, edge)

    return graph

#####################################
def get_for_annotated(view, function):
    global bridge

    if not bridge or not bridge.is_alive():
        logging.error("[MCAD] Bridge is not initialized")
        return

    info_ctx = get_info_ctx_for_annotated(function)

    client = GRPCClient()
    response = client.request_cycle_counts(info_ctx)

    g = generate_graph(response, info_ctx)
    show_graph_report("MCAD Trace Graph", g)

    del info_ctx
    del response
    del g

def get_for_function(view, function):
    global bridge

    if not bridge or not bridge.is_alive():
        logging.error("[MCAD] Bridge is not initialized")
        return

    info_ctx = get_info_ctx_for_function(function)

    client = GRPCClient()
    response = client.request_cycle_counts(info_ctx)

    g = generate_graph(response, info_ctx)
    show_graph_report("MCAD Trace Graph", g)

    del info_ctx
    del response
    del g

def start(view):
    global bridge

    if not bridge:
        view.create_tag_type("Trace Member", "🌊")
        bridge = MCADBridge(view)

    bridge.start()

def stop(view):
    global bridge

    bridge.stop()

# Commands
PluginCommand.register("MCAD\\1. Start server", "Start server", start)

PluginCommand.register_for_function("MCAD\\2. Get Cycle Counts for function", "Retrieve cycle counts for the entire function", get_for_function)
PluginCommand.register_for_function("MCAD\\3. Get Cycle Counts for annotated", "Retrieve cycle counts for annotated blocks", get_for_annotated)

PluginCommand.register("MCAD\\4. Stop server", "Stop MCAD server", stop)
