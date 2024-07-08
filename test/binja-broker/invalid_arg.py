# Test to check if BinjaBroker can handle invalid instructions.
from utils import binja_client

G = binja_client()
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
