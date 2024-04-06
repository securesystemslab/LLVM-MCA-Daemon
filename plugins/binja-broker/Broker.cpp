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
    unsigned CycleReady = 0;
    unsigned CycleExecuted = 0;
    bool IsUnderPressure = false;
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
            cc->set_is_under_pressure(CountStore[i].IsUnderPressure);
        }

        HasHandledInput.store(true);

        return grpc::Status::OK;
    }

public:
    BinjaBridge() {}
    bool Running = true;
    std::queue<BinjaInstructions::Instruction> InsnQueue;
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
            BridgeRef.CountStore.insert(std::make_pair(index, InstructionEntry{}));
        }

        switch (Event.Type) {
        case mca::HWInstructionEvent::GenericEventType::Ready: {
            BridgeRef.CountStore[index].CycleReady = CurrentCycle;
        }
        case mca::HWInstructionEvent::GenericEventType::Executed: {
             BridgeRef.CountStore[index].CycleExecuted = CurrentCycle;
        }
        default: break;
        }
    }

    void onEvent(const mca::HWStallEvent &Event) {
        std::cout << "[BINJA] Stall !\n";
    }

    void onEvent(const mca::HWPressureEvent &Event) {
        std::cout << "[BINJA] Pressure !\n";
        for (const auto &inst : Event.AffectedInstructions) {
            const unsigned index = inst.getSourceIndex();

            if (!BridgeRef.CountStore.count(index)) {
                BridgeRef.CountStore.insert(std::make_pair(index, InstructionEntry{}));
            }

            BridgeRef.CountStore[index].IsUnderPressure = true;
        }
    }

    void onCycleEnd() override { ++CurrentCycle; }
};

class BinjaBroker : public Broker {
    std::unique_ptr<MCDisassembler> DisAsm;
    const Target &TheTarget;
    MCContext &Ctx;
    const MCSubtargetInfo &STI;
    BinjaBridge Bridge;
    std::unique_ptr<RawListener> Listener;
    std::vector<std::unique_ptr<MCInst>> Local_MCIS;
    const std::string ListenAddr, ListenPort;

    std::unique_ptr<std::jthread> ServerThread;
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

                unsigned i = 0;
                while (!Bridge.InsnQueue.empty()) {
                    auto insn = Bridge.InsnQueue.front();
                    auto insn_bytes = insn.opcode();

                    SmallVector<uint8_t, 4> instructionBuffer{};
                    for (uint8_t c : insn_bytes) {
                        instructionBuffer.push_back(c);
                    }
                    ArrayRef<uint8_t> InstBytes(instructionBuffer);

                    auto MCI = std::make_unique<MCInst>();
                    uint64_t DisAsmSize;
                    auto Disassembled = DisAsm->getInstruction(*MCI, DisAsmSize, InstBytes, 0, nulls());

                    MCIS[i] = MCI.get();
                    Local_MCIS.push_back(std::move(MCI));

                    ++i;
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
    struct Options {
        StringRef ListenAddress, ListenPort;

        // Initialize default values
        Options();

        // Construct from command line options
        Options(int argc, const char *const *argv);
    };

    BinjaBroker(const Options &Opts,
                const MCSubtargetInfo &MSTI, 
                MCContext &C, 
                const Target &T)
        : ListenAddr(Opts.ListenAddress.str()), ListenPort(Opts.ListenPort.str()),
          TheTarget(T), 
          Ctx(C), 
          STI(MSTI), 
          Bridge(BinjaBridge()) {
        ServerThread = std::make_unique<std::jthread>(&BinjaBroker::serverLoop, this);
        DisAsm.reset(TheTarget.createMCDisassembler(STI, Ctx));
        Listener = std::make_unique<RawListener>(Bridge);
    }

    RawListener* getListener() {
        return Listener.get();
    }

    ~BinjaBroker() {
        server->Shutdown();
    }
};

BinjaBroker::Options::Options() 
    : ListenAddress("0.0.0.0"), ListenPort("50052") {}

BinjaBroker::Options::Options(int argc, const char *const *argv) 
    : BinjaBroker::Options() {
    for (int i = 0; i < argc; ++i) {
        StringRef Arg(argv[i]);

        if (Arg.startswith("-host") && Arg.contains("=")) {
            auto RawHost = Arg.split("=").second;
            if (RawHost.contains(':')) {
                std::tie(ListenAddress, ListenPort) = RawHost.split(':');
            } else {
                ListenAddress = RawHost;
            }
        }
    }
}

void BinjaBroker::serverLoop() {
    std::string srv_addr = ListenAddr + ':' + ListenPort;

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
              BinjaBroker::Options BrokerOpts(argc, argv);
              auto binja_broker = std::make_unique<BinjaBroker>(BrokerOpts,
                                                                BF.getSTI(), BF.getCtx(), 
                                                                BF.getTarget());
              BF.registerListener(binja_broker->getListener());
              BF.setBroker(std::move(binja_broker));
          }};
}
