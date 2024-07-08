# BinjaBroker sanity test
from utils import grpc_client

G = binja_client()

instructions = []
instructions.append(G.get_instruction([0x48, 0x01, 0xD8]))
instructions.append(G.get_instruction([0x48, 0x89, 0xCB]))
instructions.append(G.get_instruction([0x0F, 0x05]))
response = G.send_instructions(instructions)

# Close connection
G.send_instructions([])
