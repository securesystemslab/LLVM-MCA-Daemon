import vivserver_pb2_grpc
import vivserver_pb2
import logging
from concurrent import futures
import vivisect
import Elf
import grpc
import click


class Service(vivserver_pb2_grpc.EmulatorServicer):
    def __init__(self, binary_path, architecture, endianness) -> None:
        # binary_path = "/home/davidanekstein/amp/Challenge-Problems-master/Challenge_03/build/program_c"
        self.binary_path = binary_path

        vw = vivisect.VivWorkspace()

        if architecture:
            vw.setMeta("Architecture", architecture)

        if endianness == "big":
            vw.setMeta("bigend", True)
        if endianness == "little":
            vw.setMeta("bigend", False)

        vw.loadFromFile(binary_path)
        vw.analyze()

        elf = Elf.elfFromFileName(binary_path)
        print(hex(elf.getBaseAddress()))

        self.emu = vw.getEmulator()
        print(list(map(lambda x: vw.getName(x), vw.getEntryPoints())))
        self.emu.setProgramCounter(vw.getEntryPoints()[0])

    def RunNumInstructions(self, request, context):
        ops = list(self.step(request.numInstructions))
        instructions = []
        for instruction, _, opCode in ops:
            print(f"instruction: {hex(instruction)}, opcode: {hex(opCode)}")

            # although the ISA may be little endian, we don't want to alter the byte order of the int that
            # vivisect returns before sending it. Big endian maintains the byte order, so that's what we
            # send it as, but this does not reflect the endianess of the instructions themselves
            instructions.append(
                vivserver_pb2.RunInstructionsReply.Instruction(
                    instruction=instruction.to_bytes(length=8, byteorder="big"),
                    opCode=opCode.to_bytes(length=8, byteorder="big"),
                )
            )
        return vivserver_pb2.RunInstructionsReply(instructions=instructions)

    def step(self, n):
        print(n)
        for i in range(1, n):
            try:
                pc_before = self.emu.getProgramCounter()
                op = self.emu.parseOpcode(self.emu.getProgramCounter())
                self.emu.stepi()
                instruction = self.emu.readMemValue(self.emu.getProgramCounter(), 4)
                opCode = op.opcode
                yield instruction, op, opCode

            except Exception as e:
                break


@click.command()
@click.argument("binary_path")
@click.option(
    "-a",
    "--architecture",
    type=click.Choice(["amd64", "arm", "ppc32-vle", "ppc64-server", "ppc32-server"]),
)
@click.option("--address", default="localhost", show_default=True)
@click.option("-p", "--port", default="50051", show_default=True)
@click.option("-e", "--endianness", type=click.Choice(["big", "little"]))
def serve(binary_path, architecture, endianness, address, port):
    uds_addresses = [f"{address}:{port}"]
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    vivserver_pb2_grpc.add_EmulatorServicer_to_server(
        Service(binary_path, architecture, endianness), server
    )
    for uds_address in uds_addresses:
        server.add_insecure_port(uds_address)
        logging.info("Server listening on: %s", uds_address)
    server.start()
    server.wait_for_termination()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    serve()
