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
#include "llvm/Support/TargetSelect.h"
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
  // Owner of all MCInst instances
  // Note that we caonnt use SmallVector<MCInst,...> here because
  // when SmallVector resize all MCInst* retrieved previously will be invalid
  SmallVector<std::unique_ptr<MCInst>, 5> MCInsts;

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

  std::mutex TBsMutex;
  SmallVector<Optional<TranslationBlock>, 8> TBs;

  // List of to-be-executed TB index
  SmallVector<size_t, 16> TBQueue;
  // Instructions that can't be completely drained from TB
  // in last round
  SmallVector<const MCInst*, 4> ResidualInsts;
  bool IsEndOfStream;
  std::mutex QueueMutex;
  std::condition_variable QueueCV;

  void initializeServer();

  std::unique_ptr<std::thread> ReceiverThread;
  void recvWorker(addrinfo *);

  void addTB(const fbs::TranslatedBlock &TB) {
    if (TB.Index() >= TBs.size()) {
      std::lock_guard<std::mutex> LK(TBsMutex);
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

    std::lock_guard<std::mutex> LK(TBsMutex);
    TBs[TB.Index()] = std::move(NewTB);
  }

  void initializeDisassembler();

  void disassemble(TranslationBlock &TB);

  void tbExec(const fbs::ExecTB &OrigTB) {
    uint32_t Idx = OrigTB.Index();
    uint64_t PC = OrigTB.PC();

    if (Idx == ~uint32_t(0U) && PC == ~uint64_t(0U)) {
      // End signal
      LLVM_DEBUG(dbgs() << "Receive end signal...\n");
      {
        std::lock_guard<std::mutex> Lock(QueueMutex);
        IsEndOfStream = true;
      }
      QueueCV.notify_one();
      return;
    }

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
    CurDisAsm(nullptr),
    IsEndOfStream(false) {
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
  static_assert(sizeof(RecvBuffer) > sizeof(flatbuffers::uoffset_t),
                "RecvBuffer is not larger than uoffset_t");
  SmallVector<uint8_t, 2048> MsgBuffer;
  while ((ClientSocktFD = accept(ServSocktFD,
                                 AI->ai_addr, &AI->ai_addrlen))) {
    if (ClientSocktFD < 0) {
      ::perror("Failed to accept client");
      continue;
    }
    LLVM_DEBUG(dbgs() << "Get a new client\n");

    while (true) {
      MsgBuffer.clear();

      bool MsgValid = false;
      flatbuffers::uoffset_t TotalMsgSize = 0U;
      do {
        ssize_t ReadLen, Offset = 0;
        if (!TotalMsgSize) {
          // Read the prefix first
          ReadLen = read(ClientSocktFD, RecvBuffer, sizeof(TotalMsgSize));
          // Reach EOF, exit normally
          if (!ReadLen)
            break;
          if (ReadLen < sizeof(TotalMsgSize)) {
            if (ReadLen < 0)
              ::perror("Failed to read prefixed size");
            else
              // Don't try to print errno after a successful
              // read, since errno is undefined in such case
              errs() << "Failed to read prefixed size";
            errs() << "\n";
            break;
          }

          TotalMsgSize =
            flatbuffers::ReadScalar<flatbuffers::uoffset_t>(RecvBuffer);
          assert(TotalMsgSize);
          LLVM_DEBUG(dbgs() << "Total message size: " << TotalMsgSize << "\n");
          Offset = sizeof(TotalMsgSize);
        }

        ReadLen = std::min(size_t(TotalMsgSize),
                           sizeof(RecvBuffer) - Offset);
        ReadLen = read(ClientSocktFD, &RecvBuffer[Offset], ReadLen);
        if (ReadLen < 0) {
          ::perror("Failed to read from client");
          errs() << "\n";
          break;
        }
        // Reach EOF, exit normally
        if (!ReadLen)
          break;

        assert(TotalMsgSize >= ReadLen);
        TotalMsgSize -= ReadLen;

        MsgBuffer.append(RecvBuffer, &RecvBuffer[ReadLen + Offset]);

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
  llvm::InitializeAllDisassemblers();

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

  // We don't want LSB to interfere the disassembling process
  if (TheTriple.isARM() || TheTriple.isThumb())
    VAddr &= (~0b1);

  LLVM_DEBUG(dbgs() << "Disassembling " << TB.RawInsts.size()
                    << " instructions\n");
  for (const auto &RawInst : TB.RawInsts) {
    ArrayRef<uint8_t> InstBytes(RawInst);
    uint64_t Index = 0U;
    Len = InstBytes.size();
    while (Index < Len) {
      LLVM_DEBUG(dbgs() << "Try to disassemble instruction " << RawInst
                        << " with Index = " << Index
                        << ", VAddr = " << format_hex(VAddr + Index, 8) << "\n");
      auto MCI = std::make_unique<MCInst>();
      Disassembled = CurDisAsm->getInstruction(*MCI, DisAsmSize,
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

      TB.MCInsts.emplace_back(std::move(MCI));
      Index += DisAsmSize;
    }
    VAddr += RawInst.size();
  }
}

int QemuBroker::fetch(MutableArrayRef<const MCInst*> MCIS, int Size) {
  if (!Size) return 0;
  if (Size < 0 || Size > MCIS.size())
    Size = MCIS.size();

  int TotalSize = Size;

  bool ReadResiduals = ResidualInsts.size();
  if (ReadResiduals) {
    int i, RS = ResidualInsts.size();
    for (i = 0; i < RS && i < Size; ++i) {
      MCIS[i] = ResidualInsts[i];
    }
    ResidualInsts.erase(ResidualInsts.begin(),
                        i >= RS? ResidualInsts.end() :
                                 ResidualInsts.begin() + i);
    Size -= i;
  }

  if (Size <= 0)
    return TotalSize;

  SmallVector<size_t, 2> TBIndices;
  {
    std::unique_lock<std::mutex> Lock(QueueMutex);
    // Only block if the queue is completely empty
    if (TBQueue.empty()) {
      if (ReadResiduals)
        return TotalSize - Size;
      else if (IsEndOfStream)
        return -1;
      else
        QueueCV.wait(Lock,
                     [this] {
                      return IsEndOfStream || !TBQueue.empty();
                     });
    }

    if (TBQueue.empty() && ResidualInsts.empty() && IsEndOfStream)
      return -1;

    int S = Size;
    // Fetch enough block indicies to fulfill the size requirement
    std::lock_guard<std::mutex> LK(TBsMutex);
    while (S > 0 && !TBQueue.empty()) {
      size_t TBIdx = TBQueue.front();
      if (TBIdx < TBs.size() && TBs[TBIdx]) {
        TBIndices.push_back(TBIdx);
        S -= TBs[TBIdx]->MCInsts.size();
      }
      TBQueue.erase(TBQueue.begin());
    }
  }

  {
    std::lock_guard<std::mutex> LK(TBsMutex);
    for (auto TBIdx : TBIndices) {
      const auto &MCInsts = TBs[TBIdx]->MCInsts;
      int i, Len = MCInsts.size();
      for (i = 0; i < Len && Size > 0; ++i, --Size) {
        MCIS[TotalSize - Size] = MCInsts[i].get();
      }
      if (Size <= 0) {
        // Put reset of the MCInst into residual queue
        // (if there is any)
        for (; i < Len; ++i) {
          ResidualInsts.push_back(MCInsts[i].get());
        }
        break;
      }
    }
  }

  return Size > 0? TotalSize - Size : TotalSize;
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
