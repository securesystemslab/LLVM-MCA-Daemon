#@author chinmay_dd
#@category Python 3
#@keybinding 
#@menupath 
#@toolbar 

import grpc
import binja_pb2
import binja_pb2_grpc
import pdb

from ghidra.program.model.listing import CodeUnit, Function
from ghidra.app.script import GhidraScript

class GRPCClient:
    def __init__(self):
        self.channel = grpc.insecure_channel("localhost:50052")
        self.stub = binja_pb2_grpc.BinjaStub(self.channel)

    def request_empty(self):
        self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instructions=[]))
        return None

    def request_cycle_counts(self):
        response = binja_pb2.CycleCounts()

        function = getFunctionContaining(currentAddress())
        insns = function.getProgram().getListing().getInstructions(function.getBody(), True)
        instructions = []

        for insn in function.getProgram().getListing().getInstructions(function.getBody(), True):
            bytez = bytes(map(lambda b: b & 0xff, insn.getBytes()))
            instructions.append(binja_pb2.BinjaInstructions.Instruction(opcode=bytez))

        response = self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instruction=instructions))

        for idx, insn in enumerate(function.getProgram().getListing().getInstructions(function.getBody(), True)):
            cycle_start = response.cycle_count[idx].ready
            cycle_end = response.cycle_count[idx].executed

            num_cycles = cycle_end - cycle_start
            setEOLComment(insn.getAddress(), str(num_cycles))

client = GRPCClient()
client.request_cycle_counts()
