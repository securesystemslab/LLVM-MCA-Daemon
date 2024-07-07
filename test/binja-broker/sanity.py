from utils import grpc_client

G = grpc_client()
G.add_instruction([0x48, 0x01, 0xD8])
G.add_instruction([0x48, 0x89, 0xCB])
G.add_instruction([0x0F, 0x05])
response = G.send_instructions()
if response["status"] == "failure":
    ex = response["exception"]
    print("RPC Error. Code: " + str(ex.code()))
    print("Details: " + ex.details())

# Close connection
G.send_empty()
