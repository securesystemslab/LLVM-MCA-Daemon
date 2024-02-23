import grpc
import binja_pb2
import binja_pb2_grpc

class GRPCClient:
    def __init__(self):
        self.channel = grpc.insecure_channel("localhost:50052")
        self.stub = binja_pb2_grpc.BinjaStub(self.channel)

    def request_cycle_counts(self, info_ctx):
        if not info_ctx:
            self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instruction=[]))
            return None

        response = binja_pb2.CycleCounts()
        for block_addr in info_ctx.block_list:
            block = info_ctx.get_block_at_addr(block_addr)
            instructions = []
            for insn in block.disassembly_text:
                w_i = info_ctx.get_instruction_at_addr(insn.address)
                instructions.append(binja_pb2.BinjaInstructions.Instruction(opcode=bytes(w_i.bytez)))

            curr_response = self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instruction=instructions))
            response.MergeFrom(curr_response)

        return response
