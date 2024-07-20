# Test to check if BinjaBroker can handle invalid instructions.
from utils import binja_client
import argparse

parser = argparse.ArgumentParser("MCAD testing")
parser.add_argument("--port", dest="port", type=int)
args = parser.parse_args()

if __name__ == "__main__":
    G = binja_client(args.port)
    instructions = []
    # Send null instruction
    instructions.append(G.get_instruction([0x00]))
    response = G.send_instructions(instructions)
    if response["status"] == "failure":
        ex = response["exception"]
        print("RPC Error. Code: " + str(ex.code()))
        print("Details: " + ex.details())
    
    # Close connection
    G.send_instructions([])
