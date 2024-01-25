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
#include "llvm/MCA/HWEventListener.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetRegistry.h"
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

#include <grpcpp/grpcpp.h>
#include "binja.grpc.pb.h"

using namespace llvm;
using namespace mcad;

#define DEBUG_TYPE "mcad-binja-broker"

struct InstructionEntry {
    unsigned CycleReady;
    unsigned CycleExecuted;
};

class BinjaBridge final : public Binja::Service {
    grpc::Status RequestCycleCounts(grpc::ServerContext *ctxt,
                                   const BinjaInstructions *insns,
                                   CycleCounts *ccs) {
        if (!insns || !Running) {
            return grpc::Status::OK;
        }
        if (insns->instruction_size() == 0) {
            Running = false;
            return grpc::Status::OK;
        }

        {
            std::lock_guard<std::mutex> Lock(QueueMutex);
            for (int i = 0; i < insns->instruction_size(); i++) {
                InsnQueue.push(insns->instruction(i));
            }
            HasHandledInput.store(false);
        }

        IsWaitingForWorker.store(true);
        while (IsWaitingForWorker.load()) {};

        for (int i = 0; i < insns->instruction_size(); i++) {
            auto* cc = ccs->add_cycle_count();
            if (!CountStore.count(i)) {
                continue;
            }
            cc->set_ready(CountStore[i].CycleReady);
            cc->set_executed(CountStore[i].CycleExecuted);
        }

        HasHandledInput.store(true);

        return grpc::Status::OK;
    }

public:
    BinjaBridge() {}
    bool Running = true;
    std::queue<BinjaInstructions::Instruction> InsnQueue;
    std::queue<CycleCounts::CycleCount> CCQueue;
    std::mutex QueueMutex;
    std::atomic<bool> IsWaitingForWorker = false; 
    std::atomic<bool> HasHandledInput = true;
    DenseMap<unsigned, InstructionEntry> CountStore;
};

class RawListener : public mca::HWEventListener {
    BinjaBridge &BridgeRef;
    unsigned CurrentCycle;

public:
    RawListener(BinjaBridge &bridge) : BridgeRef(bridge), CurrentCycle(0U) {}

    void onEvent(const mca::HWInstructionEvent &Event) {
        const mca::Instruction &inst = *Event.IR.getInstruction();
        const unsigned index = Event.IR.getSourceIndex();

        if (!BridgeRef.CountStore.count(index)) {
            BridgeRef.CountStore.insert(std::make_pair(index, InstructionEntry{0, 0}));
        }

        switch (Event.Type) {
        case mca::HWInstructionEvent::GenericEventType::Executed: {
            BridgeRef.CountStore[index].CycleReady = CurrentCycle;
        }
        case mca::HWInstructionEvent::GenericEventType::Ready: {
             BridgeRef.CountStore[index].CycleExecuted = CurrentCycle;
        }
        default: break;
        }
    }

    void onEvent(const mca::HWStallEvent &Event) {}

    void onEvent(const mca::HWPressureEvent &Event) {}

    void onCycleEnd() override { ++CurrentCycle; }
};

class BinjaBroker : public Broker {
    std::unique_ptr<MCDisassembler> DisAsm;
    const Target &TheTarget;
    MCContext &Ctx;
    const MCSubtargetInfo &STI;
    BinjaBridge Bridge;
    uint32_t TotalNumTraces;
    std::unique_ptr<RawListener> Listener;

    std::unique_ptr<std::thread> ServerThread;
    std::unique_ptr<grpc::Server> server;

    void serverLoop();

    void signalWorkerComplete() {
        Bridge.IsWaitingForWorker.store(false);
    }

    int fetch(MutableArrayRef<const MCInst *> MCIS, int Size,
              Optional<MDExchanger> MDE) override {
        return fetchRegion(MCIS, Size, MDE).first;
    }

    std::pair<int, RegionDescriptor>
    fetchRegion(MutableArrayRef<const MCInst *> MCIS, int Size = -1,
                Optional<MDExchanger> MDE = None) override {
        {
            {
                std::lock_guard<std::mutex> Lock(Bridge.QueueMutex);
                if (!Bridge.Running) {
                    return std::make_pair(-1, RegionDescriptor(true));
                }

                if (Bridge.HasHandledInput.load()) {
                    return std::make_pair(0, RegionDescriptor(false));
                }

                if (Size < 0 || Size > MCIS.size()) {
                    Size = MCIS.size();
                }

                int num_insn = Bridge.InsnQueue.size();
                if (num_insn < Size) {
                    Size = num_insn;
                }

                for (int i = 0; i < Size; i++) {
                    auto insn = Bridge.InsnQueue.back();
                    auto insn_bytes = insn.opcode();

                    SmallVector<uint8_t, 4> instructionBuffer;
                    for (uint8_t c : insn_bytes) {
                        instructionBuffer.push_back(c);
                    }
                    ArrayRef<uint8_t> InstBytes(instructionBuffer);

                    auto MCI = std::make_shared<MCInst>();
                    uint64_t DisAsmSize;
                    auto Disassembled = DisAsm->getInstruction(*MCI, DisAsmSize, InstBytes, 0, nulls());

                    MCIS[i] = MCI.get();

                    ++TotalNumTraces;
                    Bridge.InsnQueue.pop();
                }
                return std::make_pair(num_insn, RegionDescriptor(true));
            }
        }
    }

    unsigned getFeatures() const override {
        return Broker::Feature_Metadata | Broker::Feature_Region; 
    }

public:
    BinjaBroker(const MCSubtargetInfo &MSTI, MCContext &C, const Target &T)
        : TheTarget(T), Ctx(C), STI(MSTI), Bridge(BinjaBridge()), TotalNumTraces(0U) {
        ServerThread = std::make_unique<std::thread>(&BinjaBroker::serverLoop, this);
        DisAsm.reset(TheTarget.createMCDisassembler(STI, Ctx));
        Listener = std::make_unique<RawListener>(Bridge);
    }

    RawListener* getListener() {
        return Listener.get();
    }

    ~BinjaBroker() {
        server->Shutdown();
        ServerThread->join();
    }
};

void BinjaBroker::serverLoop() {
    std::string srv_addr("localhost:50052");

    grpc::ServerBuilder builder;
    builder.AddListeningPort(srv_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&Bridge);
    server = builder.BuildAndStart();

    std::cout << "Server listening on " << srv_addr << std::endl;
    server->Wait();
}

extern "C" ::llvm::mcad::BrokerPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
mcadGetBrokerPluginInfo() {
    return {LLVM_MCAD_BROKER_PLUGIN_API_VERSION, "BinjaBroker", "v0.1",
          [](int argc, const char *const *argv, BrokerFacade &BF) {
              auto binja_broker = std::make_unique<BinjaBroker>(
                      BF.getSTI(), BF.getCtx(), BF.getTarget());
              BF.registerListener(binja_broker->getListener());
              BF.setBroker(std::move(binja_broker));
          }};
}
