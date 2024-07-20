# Test to check if BinjaBroker shuts down reliably on sending empty instructions list.
from utils import binja_client
import argparse

parser = argparse.ArgumentParser("MCAD testing")
parser.add_argument("--port", dest="port", type=int)
args = parser.parse_args()

if __name__ == "__main__":
    G = binja_client(args.port)
    # Send empty instructions list
    response = G.send_instructions([])
    if response["status"] == "failure":
        ex = response["exception"]
        print("RPC Error. Code: " + str(ex.code()))
        print("Details: " + ex.details())
    
    # Close connection
    response = G.send_instructions([])
    if response["status"] == "failure":
        ex = response["exception"]
        print("RPC Error. Code: " + str(ex.code()))
        print("Details: " + ex.details())
