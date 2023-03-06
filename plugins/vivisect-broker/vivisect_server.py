import vivserver_pb2_grpc
import vivserver_pb2
import logging
from concurrent import futures
import vivisect
import Elf
import grpc
binary_path = "/home/davidanekstein/amp/Challenge-Problems-master/Challenge_03/build/program_c"
binary_path = "/home/davidanekstein/amp/main"

vw = vivisect.VivWorkspace()
vw.setMeta('Architecture', 'arm')
vw.setMeta('bigend', False)
vw.loadFromFile(binary_path)
vw.analyze()

elf = Elf.elfFromFileName(binary_path)


emu = vw.getEmulator()
print(list(map(lambda x: vw.getName(x), vw.getEntryPoints())))
emu.setProgramCounter(vw.getEntryPoints()[0])
print(hex(elf.getBaseAddress()))


class Greeter(vivserver_pb2_grpc.EmulatorServicer):

    def RunNumInstructions(self, request, context):
        ops = list(self.step(request.numInstructions))
        instructions = []
        print(ops)
        for (instruction, _, opCode) in ops:
            print(hex(instruction), hex(opCode))
            instructions.append(vivserver_pb2.RunInstructionsReply.Instruction(
                instruction=instruction.to_bytes(length=8, byteorder='little'), opCode=opCode.to_bytes(length=8, byteorder='little')))
        return vivserver_pb2.RunInstructionsReply(instructions=instructions)

    def step(self, n):
        print(n)
        for i in range(1, n):
            try:
                pc_before = emu.getProgramCounter()
                op = emu.parseOpcode(emu.getProgramCounter())
                emu.stepi()
                instruction = emu.readMemValue(
                    emu.getProgramCounter(), 4)
                opCode = op.opcode
                yield instruction, op, opCode
            except Exception as e:
                pass


def serve():
    uds_addresses = ['unix:helloworld.sock']
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    vivserver_pb2_grpc.add_EmulatorServicer_to_server(Greeter(), server)
    for uds_address in uds_addresses:
        server.add_insecure_port(uds_address)
        logging.info('Server listening on: %s', uds_address)
    server.start()
    server.wait_for_termination()


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    serve()
