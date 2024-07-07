import grpc
import binja_pb2
import binja_pb2_grpc
import ipdb

class grpc_client:
    def __init__(self):
        self.channel = grpc.insecure_channel("localhost:50052")
        self.stub = binja_pb2_grpc.BinjaStub(self.channel)
        self.instructions = []

    def add_instruction(self, insn_bytes):
        self.instructions.append(binja_pb2.BinjaInstructions.Instruction(opcode=bytes(bytearray(insn_bytes))))

    def send_empty(self):
        self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instruction=[]))

    def send_instructions(self):
        response = {}
        try:
            result = self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instruction=self.instructions))
            response["status"] = "success"
            response["result"] = result
        except Exception as ex:
            response["status"] = "failure"
            response["exception"] = ex
        return response
