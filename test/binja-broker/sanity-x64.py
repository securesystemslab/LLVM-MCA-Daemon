# BinjaBroker sanity test
from utils import binja_client
import argparse

parser = argparse.ArgumentParser("MCAD testing")
parser.add_argument("--port", dest="port", type=int)
args = parser.parse_args()

if __name__ == "__main__":
    G = binja_client(args.port)
    
    instructions = []
    # add rax, rbx
    instructions.append(G.get_instruction([0x48, 0x01, 0xD8]))
    # mov rbx, rcx
    instructions.append(G.get_instruction([0x48, 0x89, 0xCB]))
    # syscall
    instructions.append(G.get_instruction([0x0F, 0x05]))
    response = G.send_instructions(instructions)

    print("STATUS: {}".format(response['status']))
    print(response['result'])
    
    # Close connection
    G.send_instructions([])
