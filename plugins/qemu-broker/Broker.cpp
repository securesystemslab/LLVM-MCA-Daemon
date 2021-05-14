#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/WithColor.h"
#include <condition_variable>
#include <memory>
#include <mutex>
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
using RawInstTy = SmallVector<uint8_t, 4>;
raw_ostream &operator<<(raw_ostream &OS, const RawInstTy &RawInst) {
  OS << "[ ";
  for (const auto Byte : RawInst) {
    OS << format_hex_no_prefix(Byte, 2) << " ";
  }
  OS << "]";
  return OS;
}


struct TranslationBlock {
  SmallVector<RawInstTy, 5> RawInsts;
  SmallVector<MCInst, 5> MCInsts;

  uint64_t VAddr;

  explicit TranslationBlock(size_t Size)
    : RawInsts(Size), VAddr(0U) {}

  // Is translated
  operator bool() const { return !MCInsts.empty(); }
};

class QemuBroker : public Broker {
  const std::string ListenAddr, ListenPort;
  int ServSocktFD;
  addrinfo *AI;

  const Target &TheTarget;
  MCContext &Ctx;
  const MCSubtargetInfo &STI;
  std::unique_ptr<MCDisassembler> DisAsm;
  // Workaround for archtectures that might switch
  // sub-target features half-way (e.g. ARM)
  std::unique_ptr<MCSubtargetInfo> SecondarySTI;
  std::unique_ptr<MCDisassembler> SecondaryDisAsm;
  MCDisassembler *CurDisAsm;
  inline MCDisassembler *useDisassembler(bool Primary = true) {
    CurDisAsm = Primary? DisAsm.get() : SecondaryDisAsm.get();
    return CurDisAsm;
  }

  SmallVector<Optional<TranslationBlock>, 8> TBs;

  // List of to-be-executed TB index
  SmallVector<size_t, 16> TBQueue;
  std::mutex QueueMutex;
  std::condition_variable QueueCV;

  void initializeServer();

  std::unique_ptr<std::thread> ReceiverThread;
  void recvWorker(addrinfo *);

  void addTB(const fbs::TranslatedBlock &TB) {
    if (TB.Index() >= TBs.size()) {
      TBs.resize(TB.Index() + 1);
    }

    const auto &Insts = *TB.Instructions();
    TranslationBlock NewTB(Insts.size());
    unsigned Idx = 0;
    auto &TBRawInsts = NewTB.RawInsts;
    for (const auto &Inst : Insts) {
      const auto &InstBytes = *Inst->Data();
      TBRawInsts[Idx++] = RawInstTy(InstBytes.begin(),
                                    InstBytes.end());
    }
    TBs[TB.Index()] = std::move(NewTB);
  }

  void initializeDisassembler();

  void disassemble(TranslationBlock &TB);

  void tbExec(const fbs::ExecTB &OrigTB) {
    unsigned Idx = OrigTB.Index();
    uint64_t PC = OrigTB.PC();

    if (Idx >= TBs.size() || !TBs[Idx]) {
      WithColor::error() << "Invalid TranslationBlock index\n";
      return;
    }

    auto &TB = *TBs[Idx];
    if (!TB) {
      // Disassemble
      TB.VAddr = PC;
      disassemble(TB);
    }

    // Put into the queue
    {
      std::lock_guard<std::mutex> Lock(QueueMutex);
      TBQueue.push_back(Idx);
    }
    QueueCV.notify_one();
  }

public:
  QemuBroker(StringRef Addr, StringRef Port,
             const MCSubtargetInfo &STI,
             MCContext &Ctx, const Target &T);

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

QemuBroker::QemuBroker(StringRef Addr, StringRef Port,
                       const MCSubtargetInfo &MSTI,
                       MCContext &C, const Target &T)
  : ListenAddr(Addr.str()), ListenPort(Port.str()),
    ServSocktFD(-1), AI(nullptr),
    TheTarget(T), Ctx(C), STI(MSTI),
    CurDisAsm(nullptr) {
  initializeDisassembler();

  initializeServer();
  // Kick off the worker thread
  ReceiverThread = std::make_unique<std::thread>(&QemuBroker::recvWorker,
                                                 this, AI);
}

void QemuBroker::initializeServer() {
  // Open the socket
  addrinfo Hints{};
  Hints.ai_family = AF_INET;
  Hints.ai_socktype = SOCK_STREAM;
  Hints.ai_flags = AI_PASSIVE;

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
}

void QemuBroker::recvWorker(addrinfo *AI) {
  assert(ServSocktFD >= 0);

  // Listen for incomming connections.
  static constexpr int ConnectionQueueLen = 1;
  listen(ServSocktFD, ConnectionQueueLen);
  outs() << "Listening on " << ListenAddr << ":" << ListenPort << "...\n";

  int ClientSocktFD;
  uint8_t RecvBuffer[1024];
  SmallVector<uint8_t, 2048> MsgBuffer;
  while (ClientSocktFD = accept(ServSocktFD,
                                AI->ai_addr, &AI->ai_addrlen)) {
    if (ClientSocktFD < 0) {
      ::perror("Failed to accept client");
      continue;
    }
    LLVM_DEBUG(dbgs() << "Get a new client\n");

    while (true) {
      MsgBuffer.clear();

      bool MsgValid = false;
      do {
        ssize_t ReadLen = read(ClientSocktFD, RecvBuffer, sizeof(RecvBuffer));
        if (ReadLen < 0) {
          ::perror("Failed to read from client");
          errs() << "\n";
          break;
        }
        // Reach EOF
        if (!ReadLen)
          break;

        LLVM_DEBUG(dbgs() << "Reading message of size "
                          << ReadLen << "\n");
        MsgBuffer.append(RecvBuffer, &RecvBuffer[ReadLen]);

        flatbuffers::Verifier V(ArrayRef<uint8_t>(MsgBuffer).data(),
                                MsgBuffer.size());
        MsgValid = fbs::VerifySizePrefixedMessageBuffer(V);
      } while (!MsgValid);

      if (!MsgValid)
        break;

      const fbs::Message *Msg = fbs::GetSizePrefixedMessage(MsgBuffer.data());
      switch (Msg->Content_type()) {
      case fbs::Msg_TranslatedBlock:
        addTB(*Msg->Content_as_TranslatedBlock());
        break;
      case fbs::Msg_ExecTB:
        tbExec(*Msg->Content_as_ExecTB());
        break;
      default:
        llvm_unreachable("Unrecoginized message type");
      }
    }

    LLVM_DEBUG(dbgs() << "Closing current client...\n");
    close(ClientSocktFD);
  }
}

void QemuBroker::initializeDisassembler() {
  DisAsm.reset(TheTarget.createMCDisassembler(STI, Ctx));
  CurDisAsm = DisAsm.get();

  const Triple &TheTriple = STI.getTargetTriple();
  if ((TheTriple.isARM() || TheTriple.isThumb()) &&
      !STI.checkFeatures("+mclass")) {
    // Need a secondary STI and DisAsm
    SubtargetFeatures Features(STI.getFeatureString());
    if (STI.checkFeatures("+thumb-mode"))
      Features.AddFeature("-thumb-mode");
    else
      Features.AddFeature("+thumb-mode");

    SecondarySTI.reset(
      TheTarget.createMCSubtargetInfo(TheTriple.getTriple(),
                                      STI.getCPU(), Features.getString()));
    SecondaryDisAsm.reset(TheTarget.createMCDisassembler(*SecondarySTI, Ctx));
  }
}

void QemuBroker::disassemble(TranslationBlock &TB) {
  if (TB) return;

  const auto &TheTriple = STI.getTargetTriple();
  if (TheTriple.isARM() || TheTriple.isThumb()) {
    if (TB.VAddr & 0b1)
      // Thumb mode
      useDisassembler(TheTriple.isThumb());
    else
      // ARM
      useDisassembler(TheTriple.isARM());
  }

  bool Disassembled;
  uint64_t DisAsmSize;
  uint64_t Len;
  uint64_t VAddr = TB.VAddr;
  MCInst MCI;

  // We don't want LSB to interfere the disassembling process
  if (TheTriple.isARM() || TheTriple.isThumb())
    VAddr &= (~0b1);

  for (const auto &RawInst : TB.RawInsts) {
    ArrayRef<uint8_t> InstBytes(RawInst);
    uint64_t Index = 0U;
    Len = InstBytes.size();
    while (Index < Len) {
      MCI.clear();
      Disassembled = CurDisAsm->getInstruction(MCI, DisAsmSize,
                                               InstBytes.slice(Index),
                                               VAddr + Index,
                                               nulls());
      if (!Disassembled) {
        WithColor::error() << "Failed to disassemble instruction: "
                           << RawInst << "\n";
        WithColor::note() << "Index = " << Index
                          << ", VAddr = " << format_hex(VAddr + Index, 8) << "\n";
        break;
      }

      if (!DisAsmSize)
        DisAsmSize = 1;

      TB.MCInsts.push_back(MCI);
      Index += DisAsmSize;
    }
    VAddr += RawInst.size();
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

      BF.setBroker(std::make_unique<QemuBroker>(Addr, Port,
                                                BF.getSTI(), BF.getCtx(),
                                                BF.getTarget()));
    }
  };
}
