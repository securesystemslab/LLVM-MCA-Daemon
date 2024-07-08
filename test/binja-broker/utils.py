import grpc
import binja_pb2
import binja_pb2_grpc
import random

class binja_client:
    def __init__(self):
        self.channel = grpc.insecure_channel("localhost:50052")
        self.stub = binja_pb2_grpc.BinjaStub(self.channel)

    def get_instruction(self, insn_bytes):
        return binja_pb2.BinjaInstructions.Instruction(opcode=bytes(bytearray(insn_bytes)))

    def send_instructions(self, instructions):
        response = {}
        try:
            result = self.stub.RequestCycleCounts(binja_pb2.BinjaInstructions(instruction=instructions))
            response["status"] = "success"
            response["result"] = result
        except Exception as ex:
            response["status"] = "failure"
            response["exception"] = ex
        return response
