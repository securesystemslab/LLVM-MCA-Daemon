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

    # mov    rbx,rcx
    # add    rax,rbx
    # cdqe / syscall
    def send_instructions2(self):
        insn_1 = binja_pb2.BinjaInstructions.Instruction(opcode=bytes(bytearray([0x48, 0x01, 0xD8])))
        insn_2 = binja_pb2.BinjaInstructions.Instruction(opcode=bytes(bytearray([0x48, 0x89, 0xCB])))
        insn_3 = binja_pb2.BinjaInstructions.Instruction(opcode=bytes(bytearray([0x0f, 0x05])))
        instructions = [insn_1, insn_2, insn_3]
        print(instructions)
        return self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instruction=instructions))

    def send_illegal_instruction(self):
        instructions = []
        #instructions.append(binja_pb2.BinjaInstructions.Instruction(opcode=bytes(b'\x0b\xf0\xd5\xf9')))
        instructions.append(binja_pb2.BinjaInstructions.Instruction(opcode=bytes(b'\x00')))
        return self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instruction=instructions))

    def send_empty(self):
        self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instruction=[]))

G = grpc_client()
response1 = G.send_instructions2()
ipdb.set_trace()
response2 = G.send_instructions()
ipdb.set_trace()
try:
    response3 = G.send_illegal_instruction()
except grpc._channel._InactiveRpcError as e:
    print("send_illegal_instruction() failed with:")
    print(e)
ipdb.set_trace()

