import grpc

import sys
import os

sys.path.append(os.path.dirname(os.path.abspath(__file__)))

import emulator_pb2
import emulator_pb2_grpc

from envi.archs.ppc.const import LOAD_INSTRS, STORE_INSTRS
from envi.archs.ppc.disasm_classes import BCTR_INSTR, BLR_INSTR

class grpc_client:
    def __init__(self, emu):
        self.channel = grpc.insecure_channel("localhost:50051")
        self.stub = emulator_pb2_grpc.EmulatorStub(self.channel)
        self.emu = emu

    def record_instruction(self, op, bytez):
        is_load = op.opcode in LOAD_INSTRS
        is_store = op.opcode in STORE_INSTRS
        is_branch_ctr = op.opcode in BCTR_INSTR
        is_branch_lr = op.opcode in BLR_INSTR

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

        branch_arg = None
        if is_branch_ctr or is_branch_lr:
            # XXX: Fix this - or make it random
            is_mispredict = True
            if is_mispredict:
                branch_arg = emulator_pb2.EmulatorActions.BranchFlow(
                    is_mispredict=is_mispredict)

        insn_arg = emulator_pb2.EmulatorActions.Instruction(opcode=opcode)
        if mem_arg is not None:
            insn_arg = emulator_pb2.EmulatorActions.Instruction(opcode=opcode,
                memory_access=mem_arg)

        if branch_arg is not None:
            insn_arg = emulator_pb2.EmulatorActions.Instruction(opcode=opcode,
                branch_flow=branch_arg)

        action_arg = emulator_pb2.EmulatorActions(instructions=[insn_arg])

        self.stub.RecordEmulatorActions(action_arg)

    def __del__(self):
        self.stub.RecordEmulatorActions(emulator_pb2.EmulatorActions(instructions=[]))
        self.channel.close()
