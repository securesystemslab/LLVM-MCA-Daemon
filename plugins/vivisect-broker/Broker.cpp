#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/MCA/MetadataCategories.h"
#include "llvm/MCA/MetadataRegistry.h"
#include "llvm/MCA/HardwareUnits/LSUnit.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/WithColor.h"

#include <queue>
#include <mutex>
#include <thread>

#include "BrokerFacade.h"
#include "Brokers/Broker.h"
#include "Brokers/BrokerPlugin.h"

#include <grpcpp/grpcpp.h>
#include "emulator.grpc.pb.h"

using namespace llvm;
using namespace mcad;

#define DEBUG_TYPE "mcad-vivisect-broker"

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

  std::unique_ptr<std::thread> ServerThread;
  std::unique_ptr<grpc::Server> server;

  void serverLoop();

  int fetch(MutableArrayRef<const MCInst *> MCIS, int Size,
            Optional<MDExchanger> MDE) override {
    return fetchRegion(MCIS, Size, MDE).first;
  }

  std::pair<int, RegionDescriptor>
  fetchRegion(MutableArrayRef<const MCInst *> MCIS, int Size = -1,
              Optional<MDExchanger> MDE = llvm::None) override {
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

        for (int i = 0; i < Size; i++) {
            auto insn_bytes = Service.InsnQueue.back().opcode();

            SmallVector<uint8_t, 4> instructionBuffer;
            for (uint8_t c : insn_bytes) {
                instructionBuffer.push_back(c);
            }
            ArrayRef<uint8_t> InstBytes(instructionBuffer);

            auto MCI = std::make_shared<MCInst>();
            uint64_t DisAsmSize;
            auto Disassembled = DisAsm->getInstruction(*MCI, DisAsmSize, InstBytes, 0, nulls());

            MCIS[i] = MCI.get();
            Service.InsnQueue.pop();
        }
        return std::make_pair(num_insn, RegionDescriptor(false));
    }
  }

    unsigned getFeatures() const override {
        return Broker::Feature_Metadata;
    }

public:
  VivisectBroker(const MCSubtargetInfo &MSTI, MCContext &C, const Target &T)
      : TheTarget(T), Ctx(C), STI(MSTI), Service(EmulatorService()) {
      ServerThread = std::make_unique<std::thread>(&VivisectBroker::serverLoop, this);
      DisAsm.reset(TheTarget.createMCDisassembler(STI, Ctx));
  }
  ~VivisectBroker() {
      server->Shutdown();
      ServerThread->join();
  }
};

void VivisectBroker::serverLoop() {
    std::string srv_addr("localhost:50051");

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
