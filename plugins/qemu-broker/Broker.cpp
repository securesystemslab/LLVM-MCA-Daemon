#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/MCA/HardwareUnits/LSUnit.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/WithColor.h"
#include <condition_variable>
#include <list>
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
#include <sys/un.h>
#include <unistd.h>

#include "BinaryRegions.h"
#include "BrokerFacade.h"
#include "Brokers/Broker.h"
#include "Brokers/BrokerPlugin.h"
#include "CustomHWUnits/MCADLSUnit.h"
#include "MetadataCategories.h"
#include "MetadataRegistry.h"
#include "RegionMarker.h"

#include "Serialization/mcad_generated.h"

using namespace llvm;
using namespace mcad;

#define DEBUG_TYPE "mcad-qemu-broker"

// Needed so the TypeID of the shared library and main executable refer to the
// same type.
extern template class Any::TypeId<MDMemoryAccess>;
extern template class Any::TypeId<MDInstrAddr>;

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
  SmallVector<RawInstTy, 8> RawInsts;
  // Owner of all MCInst instances
  // Note that we caonnt use SmallVector<MCInst,...> here because
  // when SmallVector resize all MCInst* retrieved previously will be invalid
  SmallVector<std::unique_ptr<MCInst>, 8> MCInsts;
  // If the size of RawInsts and MCInsts don't match (e.g. A single raw
  // instruction is disassembled into multiple MCInst), this maps from the
  // RawInsts index to MCInsts index.
  DenseMap<unsigned, unsigned> SkewIndicies;
  // Virtual addresses of the disassembled instructions in MCInst
  SmallVector<uint64_t, 8> MCInstAddrs;
  // Sizes of the disassembled MCInsts
  SmallVector<unsigned, 8> MCInstSizes;

  // The start address of this TB
  uint64_t VAddr;
  // Address offsets to each MCInst in this TB
  SmallVector<uint8_t, 8> VAddrOffsets;

  // Whether a MCInst has a begin mark
  BitVector BeginMarks;
  BitVector EndMarks;

  explicit TranslationBlock(size_t Size)
    : RawInsts(Size), VAddr(0U) {}

  // Is translated
  operator bool() const { return !MCInsts.empty(); }
};

// instruction index (in the TB) -> Memory access descriptor
// The indicies are always sorted
using MemoryAccessEntry = std::pair<unsigned, MDMemoryAccess>;
using MemoryAccessChain = SmallVector<MemoryAccessEntry, 4>;

class QemuBroker : public Broker {
  const std::string ListenAddr, ListenPort;
  int ServSocktFD;
  addrinfo *AI;

  // Max number of connection to accept before fully
  // cease operation. Or 0 for no limit.
  // By default this value is one.
  unsigned MaxNumAcceptedConnection;

  std::unique_ptr<qemu_broker::BinaryRegions> BinRegions;
  const qemu_broker::BinaryRegion *CurBinRegion;

  uint64_t CodeStartAddress;

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
  SmallVector<std::optional<TranslationBlock>, 8> TBs;

  struct TBSlice {
    // TB index
    size_t Index;
    // Creating a slice of [BeginIdx, EndIdx)
    uint16_t BeginIdx, EndIdx;

    // If it's non-null it's end of region
    const qemu_broker::BinaryRegion *Region;

    // Puting the memory access array as an class member
    // would induce lots of overhead since there are tons
    // of moving/copying actions on TBSlice. Therefore we decided
    // to only put its pointer here.
    // Normally we would use std::unique_ptr here but again, extensive
    // amount of moving/copying on TBSlice will make the performance
    // worse since std::unique_ptr does even more checks underlying.
    // Thus, though it sounds awkward, manually releasing the
    // memory at the right time point is actually the best solution
    MemoryAccessChain *MemoryAccesses;

    size_t size() const { return EndIdx - BeginIdx; }

    TBSlice()
      : Index(0U),
        BeginIdx(0u), EndIdx(0u),
        Region(nullptr), MemoryAccesses(nullptr) {}

    TBSlice(size_t Index,
            uint16_t BeginIdx, uint16_t EndIdx,
            const qemu_broker::BinaryRegion *Region,
            MemoryAccessChain *MemAccesses)
      : Index(Index),
        BeginIdx(BeginIdx), EndIdx(EndIdx),
        Region(Region), MemoryAccesses(MemAccesses) {}

    // Return another slice whose EndIdx is SplitPoint
    TBSlice split(uint16_t SplitPoint) {
      assert(SplitPoint > BeginIdx && SplitPoint < EndIdx);
      // The default copy ctor can't be generated here
      // so we're manually copying stuff
      TBSlice NewSlice;
      NewSlice.Index = Index;
      NewSlice.BeginIdx = BeginIdx;
      NewSlice.EndIdx = SplitPoint;
      NewSlice.Region = Region;
      // spliting the memory access chain
      if (MemoryAccesses) {
        auto Compare = [](const MemoryAccessEntry &MAE, unsigned Idx) {
          return MAE.first < Idx;
        };
        auto MASplit = llvm::lower_bound(*MemoryAccesses,
                                         SplitPoint, Compare);
        NewSlice.MemoryAccesses = new MemoryAccessChain(
          MemoryAccesses->begin(), MASplit);
        MemoryAccesses->erase(MemoryAccesses->begin(), MASplit);
      }

      // Update the current slice
      BeginIdx = SplitPoint;

      return std::move(NewSlice);
    }

    void release() {
      if (MemoryAccesses) {
        delete MemoryAccesses;
        MemoryAccesses = nullptr;
      }
    }
  };

  // List of to-be-executed TB index
  std::list<TBSlice> TBQueue;
  bool IsEndOfStream;
  std::mutex QueueMutex;
  std::condition_variable QueueCV;

  bool EnableMemAccessMD;

  uint32_t TotalNumTraces;

  bool EnableTimer;
  llvm::TimerGroup Timers;

  void initializeServer();
  void initializeUnixServer();

  std::unique_ptr<std::thread> ReceiverThread;
  void recvWorker();

  void addTB(const fbs::TranslatedBlock &TB) {
    static Timer TheTimer("addTB", "Adding new TB", Timers);
    TimeRegion TR(TheTimer);

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

  void handleMetadata(const fbs::Metadata &MD) {
    CodeStartAddress = MD.LoadAddr();
  }

  void tbExec(const fbs::ExecTB &OrigTB) {
#ifndef NDEBUG
    // Note that tbExec is on an extremely hot path of this Broker,
    // so even starting and stoping a timer will cause
    // signiticant performance overhead.
    static Timer TheTimer("tbExec", "Decoding executed TB",
                          Timers);
    TimeRegion TR(TheTimer);
#endif

    using namespace qemu_broker;
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

    // Binary regions handler
    uint16_t BeginIdx = 0u, EndIdx = ~uint16_t(0u);
    const BinaryRegion *Region = nullptr;
    auto handleBinaryRegions
      = [&,this](llvm::function_ref<void(size_t,bool)> Handler) {
      size_t i = 0U, S = TB.VAddrOffsets.size();
      if (!CurBinRegion) {
        // Only reset Begin / End index if we're in trimming mode
        if (BinRegions->getOperationMode() == BinaryRegions::M_Trim)
          BeginIdx = EndIdx;
        if (TB.VAddr >= CodeStartAddress) {
          // Watch if there is any match on starting address
          uint64_t VA = TB.VAddr - CodeStartAddress;
          for (; i != S; ++i) {
            uint8_t Offset = TB.VAddrOffsets[i];
            CurBinRegion = BinRegions->lookup(VA + uint64_t(Offset));
            if (CurBinRegion)
              break;
          }
          if (i != S) {
            Handler(i, true);
            LLVM_DEBUG(dbgs() << "Start to analyze region "
                              << CurBinRegion->Description
                              << " @ addr = " << format_hex(VA, 16)
                              << "\n");
          }
        }
      }

      if (CurBinRegion && TB.VAddr >= CodeStartAddress) {
        // Watch if any instruction hit the ending address
        uint64_t VA = TB.VAddr - CodeStartAddress;
        for (; i != S; ++i) {
          uint8_t Offset = TB.VAddrOffsets[i];
          if (CurBinRegion->EndAddr == VA + uint64_t(Offset))
            break;
        }
        if (i != S) {
          Handler(i, false);
          LLVM_DEBUG(dbgs() << "Terminating region "
                            << CurBinRegion->Description
                            << "\n");
          CurBinRegion = nullptr;
        }
      }
    };

    if (!TB) {
      TB.VAddr = PC;
      // Disassemble
      disassemble(TB);
      const auto &TheTriple = STI.getTargetTriple();
      if (TheTriple.isARM() || TheTriple.isThumb())
        TB.VAddr &= (~0b1);

      if (BinRegions && BinRegions->size() &&
          BinRegions->getOperationMode() == BinaryRegions::M_Mark)
        handleBinaryRegions(
          [&,this](size_t MCInstIdx, bool IsBegin) {
            if (IsBegin)
              TB.BeginMarks.set(MCInstIdx);
            else
              TB.EndMarks.set(MCInstIdx);
          });
    }

    if (BinRegions && BinRegions->size() &&
        BinRegions->getOperationMode() == BinaryRegions::M_Trim)
      handleBinaryRegions(
        [&,this](size_t MCInstIdx, bool IsBegin) {
          if (IsBegin) {
            BeginIdx = MCInstIdx;
          } else {
            // End of region
            EndIdx = MCInstIdx + 1;
            Region = CurBinRegion;
          }
        });

    // Empty slice
    if (BeginIdx == EndIdx)
      return;

    // Handle memory accesses
    MemoryAccessChain *MemAccesses = nullptr;
    const auto *FbMAs = OrigTB.MemAccesses();
    if (EnableMemAccessMD && FbMAs && FbMAs->size()) {
      MemAccesses = new MemoryAccessChain();
      for (const auto *FbMA : *FbMAs) {
        unsigned InstIdx = FbMA->Index();
        bool IsStore = FbMA->IsStore();
        uint64_t Addr = FbMA->VAddr();
        unsigned Size = FbMA->Size();

        if (TB.SkewIndicies.count(InstIdx))
          InstIdx = TB.SkewIndicies.lookup(InstIdx);

        // Honoring the index range
        if (InstIdx < BeginIdx || InstIdx >= EndIdx)
          continue;

        if (MemAccesses->size()) {
          auto &LastEntry = MemAccesses->back();
          if (LastEntry.first == InstIdx) {
            // Merge the entry
            auto &MA = LastEntry.second;
            MA.append(IsStore, Addr, Size);
            continue;
          }
        }

        MemAccesses->emplace_back(
            std::make_pair(InstIdx, MDMemoryAccess{IsStore, Addr, Size}));
      }
    }

    // Put into the queue
    {
      std::lock_guard<std::mutex> Lock(QueueMutex);
      TBQueue.emplace_back(Idx, BeginIdx, EndIdx, Region, MemAccesses);
    }
    QueueCV.notify_one();
  }

public:
  struct Options {
    // If ListenPort is empty, use Unix socket.
    StringRef ListenAddress, ListenPort;

    unsigned MaxNumConnections;

    StringRef BinaryRegionsManifestFile;
    qemu_broker::BinaryRegions::Mode BinaryRegionsOpMode;

    bool EnableMemoryAccessMD;

    bool EnableTimer;

    // Initialize the default values
    Options();

    // Constructed from command line options
    Options(int argc, const char *const *argv);
  };

  QemuBroker(const Options &Opts,
             const MCSubtargetInfo &STI,
             MCContext &Ctx, const Target &T);

  unsigned getFeatures() const override {
    unsigned Features = Broker::Feature_Metadata;
    if (BinRegions && BinRegions->size())
      Features |= Broker::Feature_Region;
    return Features;
  }

  int fetch(MutableArrayRef<const MCInst*> MCIS, int Size = -1,
            std::optional<MDExchanger> MDE = std::nullopt) override;

  std::pair<int, RegionDescriptor>
  fetchRegion(MutableArrayRef<const MCInst*> MCIS, int Size = -1,
              std::optional<MDExchanger> MDE = std::nullopt) override;

  ~QemuBroker() {
    if(ReceiverThread) {
      ReceiverThread->join();

      errs() << "Cleaning up worker thread...\n";
      if (ServSocktFD >= 0)
        close(ServSocktFD);
      if (AI)
        freeaddrinfo(AI);
    }

    if (!EnableTimer)
      Timers.clear();
  }
};
} // end anonymous namespace

QemuBroker::QemuBroker(const QemuBroker::Options &Opts,
                       const MCSubtargetInfo &MSTI,
                       MCContext &C, const Target &T)
  : ListenAddr(Opts.ListenAddress.str()), ListenPort(Opts.ListenPort.str()),
    ServSocktFD(-1), AI(nullptr),
    MaxNumAcceptedConnection(Opts.MaxNumConnections),
    CurBinRegion(nullptr),
    CodeStartAddress(0U),
    TheTarget(T), Ctx(C), STI(MSTI),
    CurDisAsm(nullptr),
    IsEndOfStream(false),
    EnableMemAccessMD(Opts.EnableMemoryAccessMD),
    TotalNumTraces(0U),
    EnableTimer(Opts.EnableTimer),
    Timers("QemuBroker", "Time spending on qemu-broker") {

  const auto &BinRegionsManifest = Opts.BinaryRegionsManifestFile;
  if (BinRegionsManifest.size()) {
    auto RegionsOrErr = qemu_broker::BinaryRegions::Create(BinRegionsManifest);
    if (!RegionsOrErr)
      handleAllErrors(RegionsOrErr.takeError(),
                      [](const ErrorInfoBase &E) {
                        E.log(WithColor::error());
                        errs() << "\n";
                      });
    else {
      BinRegions = std::move(*RegionsOrErr);
      BinRegions->setOperationMode(Opts.BinaryRegionsOpMode);
    }
  }

  initializeDisassembler();

  initializeServer();
  // Kick off the worker thread
  ReceiverThread = std::make_unique<std::thread>(&QemuBroker::recvWorker, this);
}

void QemuBroker::initializeServer() {
  if (ListenPort.empty()) {
    initializeUnixServer();
    return;
  }

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

void QemuBroker::initializeUnixServer() {
  if ((ServSocktFD = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    errs() << "Error creating socket: " << std::strerror(errno) << "\n";
    exit(1);
  }

  unlink(ListenAddr.c_str());

  sockaddr_un Addr;
  Addr.sun_family = AF_UNIX;
  strcpy(Addr.sun_path, ListenAddr.c_str());

  // Bind the socket to the desired port.
  if (bind(ServSocktFD, (sockaddr *)&Addr, sizeof(Addr)) < 0) {
    errs() << "Error on binding: " << std::strerror(errno) << "\n";
    exit(1);
  }
}

static constexpr unsigned RECV_BUFFER_SIZE = 1024;

void QemuBroker::recvWorker() {
  assert(ServSocktFD >= 0);

  // Listen for incomming connections.
  static constexpr int ConnectionQueueLen = 1;
  listen(ServSocktFD, ConnectionQueueLen);
  outs() << "Listening on " << ListenAddr;
  if (ListenPort.size())
    outs() << ":" << ListenPort;
  outs() << "...\n";

  int ClientSocktFD;
  uint8_t RecvBuffer[RECV_BUFFER_SIZE];
  static_assert(sizeof(RecvBuffer) > sizeof(flatbuffers::uoffset_t),
                "RecvBuffer is not larger than uoffset_t");
  SmallVector<uint8_t, RECV_BUFFER_SIZE> MsgBuffer;
  while ((ClientSocktFD = accept(ServSocktFD, nullptr, nullptr))) {
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
      case fbs::Msg_Metadata:
        handleMetadata(*Msg->Content_as_Metadata());
        break;
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

    if (MaxNumAcceptedConnection > 0 &&
        --MaxNumAcceptedConnection == 0)
      break;
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
  uint64_t StartVAddr = TB.VAddr;
  // We don't want LSB to interfere the disassembling process
  if (TheTriple.isARM() || TheTriple.isThumb())
    StartVAddr &= (~0b1);

  uint64_t VAddr = StartVAddr;

  LLVM_DEBUG(dbgs() << "Disassembling " << TB.RawInsts.size()
                    << " instructions\n");
  unsigned RawInstIdx = 0U;
  unsigned SkewIdxOffset = 0U;
  for (const auto &RawInst : TB.RawInsts) {
    if (SkewIdxOffset > 0)
      TB.SkewIndicies[RawInstIdx] = RawInstIdx + SkewIdxOffset;

    ArrayRef<uint8_t> InstBytes(RawInst);
    uint64_t Index = 0U;
    Len = InstBytes.size();
    unsigned NumMCInsts = 0;
    while (Index < Len) {
      LLVM_DEBUG(dbgs() << "Try to disassemble instruction " << RawInst
                        << " with Index = " << Index
                        << ", VAddr = " << format_hex(VAddr + Index, 16) << "\n");
      auto MCI = std::make_unique<MCInst>();
      Disassembled = CurDisAsm->getInstruction(*MCI, DisAsmSize,
                                               InstBytes.slice(Index),
                                               VAddr + Index,
                                               nulls());
      if (!Disassembled) {
        WithColor::error() << "Failed to disassemble instruction: "
                           << RawInst << "\n";
        WithColor::note() << "Index = " << Index
                          << ", VAddr = " << format_hex(VAddr + Index, 16) << "\n";
        break;
      }

      if (!DisAsmSize)
        DisAsmSize = 1;

      TB.MCInsts.emplace_back(std::move(MCI));
      TB.MCInstAddrs.emplace_back(VAddr + Index);
      TB.MCInstSizes.emplace_back(DisAsmSize);
      ++NumMCInsts;
      TB.VAddrOffsets.emplace_back(VAddr + Index - StartVAddr);
      Index += DisAsmSize;

      if (NumMCInsts > 1)
        // Index skew
        ++SkewIdxOffset;
    }
    VAddr += RawInst.size();
    ++RawInstIdx;
  }

  TB.BeginMarks.resize(TB.MCInsts.size());
  TB.EndMarks.resize(TB.MCInsts.size());
}

std::pair<int, Broker::RegionDescriptor>
QemuBroker::fetchRegion(MutableArrayRef<const MCInst*> MCIS, int Size,
                        std::optional<MDExchanger> MDE) {
  static Timer TheTimer("fetchRegion", "Fetching a region", Timers);
  TimeRegion TR(TheTimer);
  using namespace qemu_broker;

  if (!Size)
    return std::make_pair(0, RegionDescriptor(false));
  if (Size < 0 || Size > MCIS.size())
    Size = MCIS.size();

  int TotalSize = Size;

  SmallVector<TBSlice, 2> SelectedSlices;
  {
    std::unique_lock<std::mutex> Lock(QueueMutex);
    // Only block if the queue is completely empty
    if (TBQueue.empty()) {
      if (IsEndOfStream)
        return std::make_pair(-1, RegionDescriptor(true));
      else
        QueueCV.wait(Lock,
                     [this] {
                      return IsEndOfStream || !TBQueue.empty();
                     });
    }

    if (TBQueue.empty() && IsEndOfStream)
      return std::make_pair(-1, RegionDescriptor(true));

    // Fetch enough block indicies to fulfill the size requirement
    std::lock_guard<std::mutex> LK(TBsMutex);
    int S = Size;
    bool EndOfRegion = false;
    while (S > 0 && !TBQueue.empty() && !EndOfRegion) {
      auto &CurSlice = TBQueue.front();
      size_t TBIdx = CurSlice.Index;
      if (TBIdx < TBs.size() && TBs[TBIdx]) {
        size_t SliceLen = std::min(TBs[TBIdx]->MCInsts.size(), CurSlice.size());
        if (SliceLen > S) {
          // We need to split the current TB slice
          TBSlice TakenSlice = CurSlice.split(uint16_t(CurSlice.BeginIdx + S));
          TakenSlice.Region = nullptr;
          SelectedSlices.emplace_back(std::move(TakenSlice));
          S = 0;
        } else {
          SelectedSlices.emplace_back(std::move(CurSlice));
          TBQueue.erase(TBQueue.begin());
          S -= SliceLen;
        }
      }
      EndOfRegion = SelectedSlices.empty()? false :
                                            bool(SelectedSlices.back().Region);
    }
  }

  {
    auto setMemoryAccessMD = [&, this](unsigned InstIdx, MDMemoryAccess &&MDA) {
      if (MDE) {
        auto &Registry = MDE->MDRegistry;
        auto &IndexMap = MDE->IndexMap;
        auto &MemAccessCat = Registry[MD_LSUnit_MemAccess];
        // Simply uses trace MCInst's sequence number
        // as index
        MemAccessCat[IndexMap[InstIdx]] = std::move(MDA);
      }
    };
    auto setRegionMarkerMD = [&,this](unsigned InstIdx, bool IsBegin) {
      if (MDE) {
        auto &Registry = MDE->MDRegistry;
        auto &IndexMap = MDE->IndexMap;
        auto &MarkerCat = Registry[mcad::MD_BinaryRegionMarkers];
        RegionMarker Val
          = IsBegin? RegionMarker::getBegin() : RegionMarker::getEnd();
        if (auto MaybeVal = MarkerCat.get<mcad::RegionMarker>(IndexMap[InstIdx]))
          MarkerCat[IndexMap[InstIdx]] = std::move(Val | *MaybeVal);
        else
          MarkerCat[IndexMap[InstIdx]] = std::move(Val);
      }
    };

    std::lock_guard<std::mutex> LK(TBsMutex);
    for (auto &Slice : SelectedSlices) {
      size_t TBIdx = Slice.Index;
      const auto &CurTB = TBs[TBIdx];
      const auto &MCInsts = CurTB->MCInsts;
      const auto &MCInstAddrs = CurTB->MCInstAddrs;
      const auto &MCInstSizes = CurTB->MCInstSizes;
      auto *MAs = Slice.MemoryAccesses;
      size_t MAIdx = 0U, NumMAs = 0U;
      if (MAs)
        NumMAs = MAs->size();

      size_t i, End = std::min(MCInsts.size(), size_t(Slice.EndIdx));
      assert(TotalSize >= Size);
      for (i = Slice.BeginIdx; i != End && Size > 0; ++i, --Size) {
        int InstIdx = TotalSize - Size;
        // Add the instruction itself to MCIS, which is the input argument from
        // which the caller will read the received instructions
        const auto *MCI = MCInsts[i].get();
        MCIS[InstIdx] = MCI;

        // Add metadata to the returned instructionsusing the "exchanger"
        // All metadata in their respective categories must be indexed by the same
        // index, since there is only one IndexMap in the Metadata exchanger.
        // We simply use trace MCInst's sequence number
        // as index
        unsigned MetadataIdx = TotalNumTraces;
        auto &Registry = MDE->MDRegistry;
        auto &IndexMap = MDE->IndexMap;
        IndexMap[InstIdx] = MetadataIdx;

        // Instruction address and size metadata
        auto &InstrAddrCat = Registry[MD_InstrAddr];
        InstrAddrCat[MetadataIdx] = MDInstrAddr { MCInstAddrs[i], MCInstSizes[i] };

        // Memory access metadata
        if (MAs && MAIdx != NumMAs) {
          if ((*MAs)[MAIdx].first == i)
            setMemoryAccessMD(InstIdx,
                              std::move((*MAs)[MAIdx++].second));
        }

        // Region markers
        if (CurTB->BeginMarks.test(i))
          setRegionMarkerMD(InstIdx, /*IsBegin=*/true);
        if (CurTB->EndMarks.test(i))
          setRegionMarkerMD(InstIdx, /*IsBegin=*/false);

        ++TotalNumTraces;
      }
      Slice.release();
    }
  }

  if(SelectedSlices.size()) {
    if (const auto *Region = SelectedSlices.back().Region)
      // End of Region
      return std::make_pair(TotalSize - Size,
                            RegionDescriptor(true, Region->Description));
  }
  return std::make_pair(TotalSize - Size, RegionDescriptor(false));
}

int QemuBroker::fetch(MutableArrayRef<const MCInst*> MCIS, int Size,
                      std::optional<MDExchanger> MDE) {
  return fetchRegion(MCIS, Size, MDE).first;
}

QemuBroker::Options::Options()
  : ListenAddress("0.0.0.0"), ListenPort(),
    MaxNumConnections(1),
    BinaryRegionsManifestFile(),
    BinaryRegionsOpMode(qemu_broker::BinaryRegions::M_Trim),
    EnableMemoryAccessMD(true),
    EnableTimer(false) {}

QemuBroker::Options::Options(int argc, const char *const *argv)
  : QemuBroker::Options() {
  for (int i = 0; i < argc; ++i) {
    StringRef Arg(argv[i]);
    // Try to parse the listening address and port
    if (Arg.starts_with("-host") && Arg.contains('=')) {
      auto RawHost = Arg.split('=').second;
      if (RawHost.contains(':'))
        // Using TCP socket.
        std::tie(ListenAddress, ListenPort) = RawHost.split(':');
      else
        // Using Unix socket.
        ListenAddress = RawHost;
    }

    // Try to parse the max number of accepted connection
    if (Arg.starts_with("-max-accepted-connection") &&
        Arg.contains('=')) {
      auto RawVal = Arg.split('=').second;
      if (RawVal.trim().getAsInteger(0, MaxNumConnections)) {
        WithColor::error() << "Invalid number: " << RawVal << "\n";
        ::exit(1);
      }
    }

    // Try to parse the binary regions manifest file
    // and operation mode
    size_t Pos = Arg.find("-binary-regions");
    if (Pos != StringRef::npos && Arg.contains('=')) {
      BinaryRegionsManifestFile = Arg.split('=').second;
      if (Pos > 1) {
        using namespace qemu_broker;
        // Starts from 1 because we want to drop the leading '-'
        StringRef Mode = Arg.slice(1, Pos);
        BinaryRegionsOpMode = StringSwitch<BinaryRegions::Mode>(Mode)
                                .Case("trim", BinaryRegions::M_Trim)
                                .Case("mark", BinaryRegions::M_Mark)
                                .Default(BinaryRegions::M_Trim);
      }
    }

    // Try to parse the memory access metadata feature flag
    if (Arg.starts_with("-disable-memory-access-md"))
      EnableMemoryAccessMD = false;

    // Try to parse the option that enables timer
    // Note that this flag is automatically appended
    // if `-enable-timer` is supplied on the `llvm-mcad` side
    if (Arg.starts_with("-enable-timer"))
      EnableTimer = true;
  }
}

extern "C" ::llvm::mcad::BrokerPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
mcadGetBrokerPluginInfo() {
  return {
    LLVM_MCAD_BROKER_PLUGIN_API_VERSION, "QemuBroker", "v0.1",
    [](int argc, const char *const *argv, BrokerFacade &BF) {
      QemuBroker::Options BrokerOpts(argc, argv);
      BF.setBroker(std::make_unique<QemuBroker>(BrokerOpts,
                                                BF.getSTI(), BF.getCtx(),
                                                BF.getTarget()));
    }
  };
}
