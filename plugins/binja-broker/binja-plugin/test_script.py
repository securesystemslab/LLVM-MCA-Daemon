import grpc
import binja_pb2
import binja_pb2_grpc
import ipdb

class grpc_client:
    def __init__(self):
        self.channel = grpc.insecure_channel("localhost:50052")
        self.stub = binja_pb2_grpc.BinjaStub(self.channel)

    # mov    rbx,rcx
    # add    rax,rbx
    # sub    rax,rcx
    def send_instructions(self):
        insn_1 = binja_pb2.BinjaInstructions.Instruction(opcode=bytes(bytearray([0x48, 0x01, 0xD8])))
        insn_2 = binja_pb2.BinjaInstructions.Instruction(opcode=bytes(bytearray([0x48, 0x89, 0xCB])))
        insn_3 = binja_pb2.BinjaInstructions.Instruction(opcode=bytes(bytearray([0x48, 0x29, 0xC8])))
        instructions = [insn_1, insn_2, insn_3]
        print(instructions)
        return self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instruction=instructions))

    def send_empty(self):
        self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instruction=[]))

G = grpc_client()
response = G.send_instructions()
# ipdb.set_trace()
