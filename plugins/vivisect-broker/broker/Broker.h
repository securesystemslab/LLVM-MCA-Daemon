#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <optional>

#include <arpa/inet.h>
#include <cstdlib>
#include <cstdio>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <grpcpp/grpcpp.h>
#include "vivserver.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;


class EmulatorClient {
 public:
  EmulatorClient(std::shared_ptr<Channel> channel)
      : stub_(Emulator::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  std::optional<RunInstructionsReply> RunInstructions(int size) {
    // Data we are sending to the server.
    RunInstructionsRequest request;
    request.set_numinstructions(size);

    // Container for the data we expect from the server.
    RunInstructionsReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The actual RPC.
    Status status = stub_->RunNumInstructions(&context, request, &reply);

    // Act upon its status.
    if (status.ok()) {
      return reply;
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return std::nullopt;
    }
  }

 private:
  std::unique_ptr<Emulator::Stub> stub_;
};
