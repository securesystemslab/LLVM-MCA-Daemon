from utils import grpc_client

G = grpc_client()
G.add_instruction([0x00])
response = G.send_instructions()
if response["status"] == "failure":
    ex = response["exception"]
    print("RPC Error. Code: " + str(ex.code()))
    print("Details: " + ex.details())

# Close connection
G.send_empty()
