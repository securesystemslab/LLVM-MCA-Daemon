import vivserver_pb2_grpc
import vivserver_pb2
import logging
import vivisect
import Elf
import grpc
import click
import envi
import envi.archs.ppc.const as eapc

from concurrent import futures


class Service(vivserver_pb2_grpc.EmulatorServicer):
    def __init__(self, binary_path, architecture, endianness) -> None:
        # binary_path = "/home/davidanekstein/amp/Challenge-Problems-master/Challenge_03/build/program_c"
        self.binary_path = binary_path

        self.vw = vivisect.VivWorkspace()

        if architecture:
            self.vw.setMeta("Architecture", architecture)

        if endianness == "big":
            self.vw.setMeta("bigend", True)
        elif endianness == "little":
            self.vw.setMeta("bigend", False)
        else:
            raise Exception(f"{endianness} not supported")

        self.vw.loadFromFile(binary_path)
        self.vw.analyze()

        elf = Elf.elfFromFileName(binary_path)
        print(hex(elf.getBaseAddress()))

        self.emu = self.vw.getEmulator(funconly=False)
        print(list(map(lambda x: self.vw.getName(x), self.vw.getEntryPoints())))
        self.entry = self.vw.getEntryPoints()[0]
        self.emu.setProgramCounter(self.entry)

        # set the link register to determine when the program
        # calls blr to hand control back to the OS
        # TODO: this is probably only going to work
        # for PowerPC or ARM where link registers are a thing
        self.emu.setRegisterByName("lr", 0xCAFECAFE)

        self.instruction_count = 0

    def RunNumInstructions(self, request, context):
        ops = list(self.step(request.numInstructions))
        instructions = []

        max_instruction_size_bytes = 0
        for instruction, _, _ in ops:
            max_instruction_size_bytes = max(
                (instruction.bit_length() + 7) // 8, max_instruction_size_bytes
            )

        for instruction, op, opCode in ops:
            # although the ISA may be little endian, we don't want to alter the byte order of the int that
            # vivisect returns for the instruction and for the opcode before sending it. Big endian maintains
            # the byte order, so that's what we send, but this does not reflect the endianess of the
            # instructions themselves
            instructions.append(
                vivserver_pb2.RunInstructionsReply.Instruction(
                    instruction=instruction.to_bytes(
                        length=max_instruction_size_bytes, byteorder="big"
                    ),
                    opCode=opCode.to_bytes(
                        length=max_instruction_size_bytes, byteorder="big"
                    ),
                )
            )
        return vivserver_pb2.RunInstructionsReply(instructions=instructions)

    def step(self, n):
        print(f"instructions requested: {n}")
        for i in range(1, n):
            try:
                pc = self.emu.getProgramCounter()
                if pc == 0xCAFECAFE:
                    print("COMPLETE")
                    break

                op = self.emu.parseOpcode(pc)
                instruction = self.emu.readMemValue(pc, 4)

                print(f"instruction: {hex(instruction)}, op: {op}, op va: {hex(op.va)}")

                self.emu.stepi()

                opCode = op.opcode
                self.instruction_count += 1

                yield instruction, op, opCode

            except Exception as e:
                print(e)
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
