# Test to check if BinjaBroker shuts down reliably on sending empty instructions list.
from utils import binja_client

G = binja_client()
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
