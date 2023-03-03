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
        for op in ops:
            print(op)
        return vivserver_pb2_grpc.vivserver__pb2.RunInstructionsReply(numInstructionsRun=len(ops))

    def step(self, n):
        for i in range(1, n):
            try:
                pc_before = emu.getProgramCounter()
                op = emu.parseOpcode(emu.getProgramCounter())
                emu.stepi()
                yield hex(emu.readMemValue(emu.getProgramCounter(), 4)), op, hex(op.opcode)
            except:
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
