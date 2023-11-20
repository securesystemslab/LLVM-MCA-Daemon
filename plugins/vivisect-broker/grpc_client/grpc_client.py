import grpc

import sys
import os

sys.path.append(os.path.dirname(os.path.abspath(__file__)))

import emulator_pb2
import emulator_pb2_grpc

from envi.archs.ppc.const import LOAD_INSTRS, STORE_INSTRS

class grpc_client:
    def __init__(self, emu):
        self.channel = grpc.insecure_channel("localhost:50051")
        self.stub = emulator_pb2_grpc.EmulatorStub(self.channel)
        self.emu = emu

    def record_instruction(self, op, bytez):
        is_load = op.opcode in LOAD_INSTRS
        is_store = op.opcode in STORE_INSTRS

        addr = None
        width = None
        if is_load or is_store:
            addr = []
            oper = None
            for operand in op.opers:
                maybe_addr = operand.getOperAddr((), emu=self.emu)
                if maybe_addr is not None:
                    oper = operand
                    addr.append(maybe_addr)

            assert(len(addr) == 1)
            addr = addr[0]
            width = oper.getWidth(emu=self.emu)

        opcode = bytes(bytez)

        mem_arg = None
        if addr is not None and width is not None:
            mem_arg = emulator_pb2.EmulatorActions.MemoryAccess(
                vaddr=addr,
                size=width,
                is_store=is_store)

        insn_arg = emulator_pb2.EmulatorActions.Instruction(opcode=opcode)
        if mem_arg is not None:
            insn_arg = emulator_pb2.EmulatorActions.Instruction(opcode=opcode,
                mem_access=mem_arg)

        action_arg = emulator_pb2.EmulatorActions(instructions=[insn_arg])

        self.stub.RecordEmulatorActions(action_arg)

    def __del__(self):
        self.stub.RecordEmulatorActions(emulator_pb2.EmulatorActions(instructions=[]))
        self.channel.close()
