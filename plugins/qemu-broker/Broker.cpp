#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/WithColor.h"
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <cstdlib>
#include <cstdio>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "BrokerFacade.h"
#include "Brokers/Broker.h"
#include "Brokers/BrokerPlugin.h"

#include "Serialization/mcad_generated.h"

using namespace llvm;
using namespace mcad;

#define DEBUG_TYPE "mcad-qemu-broker"

namespace {
class QemuBroker : public Broker {
  const std::string ListenAddr, ListenPort;
  int ServSocktFD;

  std::unique_ptr<std::thread> ReceiverThread;

  void recvWorker(addrinfo *);

public:
  QemuBroker(StringRef Addr, StringRef Port);

  int fetch(MutableArrayRef<const MCInst*> MCIS, int Size = -1) override;

  ~QemuBroker() {
    if(ReceiverThread) {
      ReceiverThread->join();

      errs() << "Cleaning up worker thread...\n";
      if (ServSocktFD >= 0) {
        close(ServSocktFD);
      }
    }
  }
};
} // end anonymous namespace

QemuBroker::QemuBroker(StringRef Addr, StringRef Port)
  : ListenAddr(Addr.str()), ListenPort(Port.str()),
    ServSocktFD(-1) {
  // Open the socket
  addrinfo Hints{};
  Hints.ai_family = AF_INET;
  Hints.ai_socktype = SOCK_STREAM;
  Hints.ai_flags = AI_PASSIVE;

  addrinfo *AI;
  if (int EC = getaddrinfo(ListenAddr.c_str(), ListenPort.c_str(),
                           &Hints, &AI)) {
    errs() << "Error setting up bind address: " << gai_strerror(EC) << "\n";
    exit(1);
  }

  // Create a socket from first addrinfo structure returned by getaddrinfo.
  if ((ServSocktFD = socket(AI->ai_family, AI->ai_socktype, AI->ai_protocol)) < 0) {
    errs() << "Error creating socket: " << std::strerror(errno) << "\n";
    exit(1);
  }

  // Avoid "Address already in use" errors.
  const int Yes = 1;
  if (setsockopt(ServSocktFD, SOL_SOCKET, SO_REUSEADDR, &Yes, sizeof(int)) == -1) {
    errs() << "Error calling setsockopt: " << std::strerror(errno) << "\n";
    exit(1);
  }

  // Bind the socket to the desired port.
  if (bind(ServSocktFD, AI->ai_addr, AI->ai_addrlen) < 0) {
    errs() << "Error on binding: " << std::strerror(errno) << "\n";
    exit(1);
  }

  ReceiverThread = std::make_unique<std::thread>(&QemuBroker::recvWorker,
                                                 this, AI);
}

void QemuBroker::recvWorker(addrinfo *AI) {
  assert(ServSocktFD >= 0);

  // Listen for incomming connections.
  static constexpr int ConnectionQueueLen = 1;
  listen(ServSocktFD, ConnectionQueueLen);
  outs() << "Listening on " << ListenAddr << ":" << ListenPort << "...\n";

  int ClientSocktFD;
  uint8_t RecvBuffer[100];
  std::vector<uint8_t> MsgBuffer;
  while (ClientSocktFD = accept(ServSocktFD,
                                AI->ai_addr, &AI->ai_addrlen)) {
    if (ClientSocktFD < 0) {
      ::perror("Failed to accept client");
      continue;
    }
    MsgBuffer.clear();

    bool MsgValid = false;
    do {
      ssize_t ReadLen = read(ClientSocktFD, RecvBuffer, sizeof(RecvBuffer));
      LLVM_DEBUG(dbgs() << "Reading message...\n");
      if (ReadLen < 0) {
        ::perror("Failed to read from client");
        errs() << "\n";
        break;
      }
      MsgBuffer.insert(MsgBuffer.cend(),
                       RecvBuffer, &RecvBuffer[ReadLen]);

      flatbuffers::Verifier V(MsgBuffer.data(), MsgBuffer.size());
      MsgValid = fbs::VerifySizePrefixedMessageBuffer(V);
    } while (!MsgValid);

    if (!MsgValid) {
      close(ClientSocktFD);
      continue;
    }

    const fbs::Message *Msg = fbs::GetSizePrefixedMessage(MsgBuffer.data());
    if (Msg) {
      LLVM_DEBUG(dbgs() << "Successfully get a message!\n");
      const auto *TB = Msg->Content_as_TranslatedBlock();
      errs() << "TB Index: " << TB->Index() << "\n";
      errs() << "TB Instructions size: " << TB->Instructions()->size() << "\n";

      close(ClientSocktFD);
      break;
    }
  }
}

int QemuBroker::fetch(MutableArrayRef<const MCInst*> MCIS, int Size) {
  return -1;
}

extern "C" ::llvm::mcad::BrokerPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
mcadGetBrokerPluginInfo() {
  return {
    LLVM_MCAD_BROKER_PLUGIN_API_VERSION, "QemuBroker", "v0.1",
    [](int argc, const char *const *argv, BrokerFacade &BF) {
      StringRef Addr = "localhost",
                Port = "9487";

      for (int i = 0; i < argc; ++i) {
        StringRef Arg(argv[i]);
        // Try to parse the listening address and port
        if (Arg.startswith("-host") && Arg.contains('=')) {
          auto RawHost = Arg.split('=').second;
          if (RawHost.contains(':')) {
            StringRef RawPort;
            std::tie(Addr, Port) = RawHost.split(':');
          }
        }
      }

      BF.setBroker(std::make_unique<QemuBroker>(Addr, Port));
    }
  };
}
