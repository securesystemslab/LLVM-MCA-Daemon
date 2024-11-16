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

#include <queue>
#include <mutex>
#include <thread>
#include <algorithm>

#include "BrokerFacade.h"
#include "Brokers/Broker.h"
#include "Brokers/BrokerPlugin.h"
#include "CustomHWUnits/MCADLSUnit.h"
#include "MetadataCategories.h"
#include "MetadataRegistry.h"

#include <grpcpp/grpcpp.h>
#include "emulator.grpc.pb.h"

using namespace llvm;
using namespace mcad;

#define DEBUG_TYPE "mcad-vivisect-broker"

// Needed so the TypeID of the shared library and main executable refer to the
// same type.
extern template class Any::TypeId<MDMemoryAccess>;
extern template class Any::TypeId<MDInstrAddr>;

class EmulatorService final : public Emulator::Service {
  grpc::Status RecordEmulatorActions(grpc::ServerContext *ctxt,
                                     const EmulatorActions *actions,
                                     NextAction *next) override {
    if (!actions || !running) {
        return grpc::Status::OK;
    }
    if (actions->instructions_size() == 0) {
        running = false;
        return grpc::Status::OK;
    }
    {
        std::lock_guard<std::mutex> Lock(QueueMutex);
        for (int i = 0; i < actions->instructions_size(); i++) {
            InsnQueue.push(actions->instructions(i));
        }
    }
    return grpc::Status::OK;
  }
public:
  EmulatorService() {}
  bool running = true;
  std::queue<EmulatorActions::Instruction> InsnQueue;
  std::mutex QueueMutex;
};

class VivisectBroker : public Broker {
    std::unique_ptr<MCDisassembler> DisAsm;
  const Target &TheTarget;
  MCContext &Ctx;
  const MCSubtargetInfo &STI;
  EmulatorService Service;
  uint64_t TotalNumTraces;

  std::unique_ptr<std::thread> ServerThread;
  std::unique_ptr<grpc::Server> server;
  std::vector<MCInst> MCI_Pool;

  void serverLoop();

  int fetch(MutableArrayRef<const MCInst *> MCIS, int Size,
            std::optional<MDExchanger> MDE) override {
    return fetchRegion(MCIS, Size, MDE).first;
  }

  std::pair<int, RegionDescriptor>
  fetchRegion(MutableArrayRef<const MCInst *> MCIS, int Size = -1,
              std::optional<MDExchanger> MDE = std::nullopt) override {
    {
        std::lock_guard<std::mutex> Lock(Service.QueueMutex);
        if (!Service.running) {
            return std::make_pair(-1, RegionDescriptor(false));
        }

        if (Size < 0 || Size > MCIS.size()) {
            Size = MCIS.size();
        }

        int num_insn = Service.InsnQueue.size();
        if (num_insn < Size) {
            Size = num_insn;
        }

        const Triple &the_triple = STI.getTargetTriple();
        for (int i = 0; i < Size; i++) {
            auto insn = Service.InsnQueue.back();
            auto insn_bytes = insn.opcode();
            if (the_triple.isLittleEndian()) {
                reverse(insn_bytes.begin(), insn_bytes.end());
            }

            SmallVector<uint8_t, 4> instructionBuffer;
            for (uint8_t c : insn_bytes) {
                instructionBuffer.push_back(c);
            }
            ArrayRef<uint8_t> InstBytes(instructionBuffer);

            // Create new MCInst and put it into our local pool storage.
            // This can be freed when the worker thread is done with it.
            MCI_Pool.emplace_back();
            MCInst &MCI = MCI_Pool.back();
            uint64_t DisAsmSize;
            auto Disassembled = DisAsm->getInstruction(MCI, DisAsmSize, InstBytes, 0, nulls());
            assert(Disassembled == MCDisassembler::DecodeStatus::Success);

            MCIS[i] = &MCI;  // return a pointer into our MCI_Pool

            // Add metadata to the fetched instruction if metadata exchanger
            // is available
            if (MDE) {
              
              // The registry stores the metadata
              auto &Registry = MDE->MDRegistry;

              // The IndexMap maps instruction index (within this region) to
              // the identifier we chose for our metadata. We chose a
              // monotonically increasing counter as the identifier for each
              // metadata entry.
              auto &IndexMap = MDE->IndexMap;
              IndexMap[i] = TotalNumTraces;

              auto &InstrAddrCat = Registry[MD_InstrAddr];
              InstrAddrCat[TotalNumTraces] = MDInstrAddr { insn.addr() };

              if (insn.has_memory_access()) {
                  auto MemAccess = insn.memory_access();
                  auto &MemAccessCat = Registry[MD_LSUnit_MemAccess];
                  MemAccessCat[TotalNumTraces] = std::move(MDMemoryAccess{
                      MemAccess.is_store(),
                      MemAccess.vaddr(),
                      MemAccess.size(),
                  });
              }

              if (insn.has_branch_flow()) {
                  auto BranchFlow = insn.branch_flow();
                  auto &Registry = MDE->MDRegistry;
                  auto &BranchFlowCat = Registry[MD_FrontEnd_BranchFlow];
                  BranchFlowCat[TotalNumTraces] = BranchFlow.is_mispredict();
              }

            }

            ++TotalNumTraces;
            Service.InsnQueue.pop();
        }
        return std::make_pair(num_insn, RegionDescriptor(false));
    }
  }

  unsigned getFeatures() const override {
    return Broker::Feature_Metadata;
  }

  void signalWorkerComplete() override {
    MCI_Pool.clear();
  }

public:
  VivisectBroker(const MCSubtargetInfo &MSTI, MCContext &C, const Target &T)
      : TheTarget(T), Ctx(C), STI(MSTI), Service(EmulatorService()), TotalNumTraces(0U) {
    ServerThread = std::make_unique<std::thread>(&VivisectBroker::serverLoop, this);
    DisAsm.reset(TheTarget.createMCDisassembler(STI, Ctx));
  }
  ~VivisectBroker() {
    server->Shutdown();
    ServerThread->join();
  }
};

void VivisectBroker::serverLoop() {
  std::string srv_addr("0.0.0.0:50051");

  grpc::ServerBuilder builder;
  builder.AddListeningPort(srv_addr, grpc::InsecureServerCredentials());
  builder.RegisterService(&Service);
  server = builder.BuildAndStart();

  std::cout << "Server listening on " << srv_addr << std::endl;
  server->Wait();
}

extern "C" ::llvm::mcad::BrokerPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
mcadGetBrokerPluginInfo() {
  return {LLVM_MCAD_BROKER_PLUGIN_API_VERSION, "VivisectBroker", "v0.1",
          [](int argc, const char *const *argv, BrokerFacade &BF) {
            // TODO: set things like target
            BF.setBroker(std::make_unique<VivisectBroker>(
              BF.getSTI(), BF.getCtx(), BF.getTarget()));
          }};
}
