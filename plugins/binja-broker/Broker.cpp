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
#include "llvm/MCA/HWEventListener.h"
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
#include <sstream>

#include "BrokerFacade.h"
#include "Brokers/Broker.h"
#include "Brokers/BrokerPlugin.h"
#include "MetadataCategories.h"
#include "MetadataRegistry.h"

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

    void shutdown() {
        Running = false;
        DoneHandlingInput.store(false);
        DoneHandlingInput.notify_all();
    }

    grpc::Status RequestCycleCounts(grpc::ServerContext *ctxt,
                                    const BinjaInstructions *insns,
                                    CycleCounts *ccs) {
        if (!Running) {
            return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                                "Previous error caused BinjaBridge shutdown.");
        }
        if (!insns) {
            shutdown();
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "NULL pointer passed for instructions.");
        }
        if (insns->instruction_size() == 0) {
            // Sending a request with empty cycle counts indicates the plugin
            // wants to shut down the server.
            shutdown();
            return grpc::Status::OK;
        }

        {
            std::lock_guard<std::mutex> Lock(QueueMutex);
            for (int i = 0; i < insns->instruction_size(); i++) {
                InsnQueue.push(insns->instruction(i));
            }
        }

        DoneHandlingInput.store(false);
        DoneHandlingInput.notify_all();

        IsWaitingForWorker.store(true);
        // Block until worker is done processing the input
        IsWaitingForWorker.wait(true);

        grpc::Status return_status = grpc::Status::OK;

        if (InstructionErrors.size() > 0) {
            std::ostringstream msg;
            msg << "Errors in instruction(s) ";
            auto it = InstructionErrors.begin();
            msg << it->first << " (" << toString(std::move(it->second)) << ")";
            ++it;
            for (; it != InstructionErrors.end(); ++it) {
              msg << ", " << it->first << " ("
                  << toString(std::move(it->second)) << ")";
            }
            msg << ".";
            InstructionErrors.clear();
            return_status = grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, msg.str());
        } else {
            for (int i = 0; i < insns->instruction_size(); i++) {
                auto* cc = ccs->add_cycle_count();
                if (!CountStore.count(i)) {
                    continue;
                }
                cc->set_ready(CountStore[i].CycleReady);
                cc->set_executed(CountStore[i].CycleExecuted);
                cc->set_is_under_pressure(CountStore[i].IsUnderPressure);
            }
        }

        DoneHandlingInput.store(true);

        return return_status;
    }

public:
    BinjaBridge() {}
    bool Running = true;
    std::queue<BinjaInstructions::Instruction> InsnQueue;
    std::mutex QueueMutex;
    std::atomic<bool> IsWaitingForWorker = false;
    std::atomic<bool> DoneHandlingInput = true;
    DenseMap<unsigned, InstructionEntry> CountStore;

    // InstructionErrors - Synchronization: 
    // - Only ever written by MCAD worker thread (BinjaBroker class) when
    //   IsWaitingForWorker == true
    // - Only ever read by gRPC thread (RequestCycleCounts) when
    //   IsWaitingForWorker == false
    DenseMap<unsigned, llvm::Error> InstructionErrors;
};

class RawListener : public mca::HWEventListener {
    BinjaBridge &BridgeRef;
    unsigned CurrentCycle;

public:
    RawListener(BinjaBridge &bridge) : BridgeRef(bridge), CurrentCycle(0U) {}

    void onEvent(const mca::HWInstructionEvent &Event) override {
        const auto &inst = *Event.IR.getInstruction();
        const unsigned index = Event.IR.getSourceIndex();

        if (!BridgeRef.CountStore.count(index)) {
            BridgeRef.CountStore.insert(std::make_pair(index, InstructionEntry{}));
        }

        switch (Event.Type) {
        //case mca::HWInstructionEvent::GenericEventType::Dispatched: {
        // We (ab-)use the LastGenericEventType in the FetchDelay stage to
        // notify of an event whenever the instruction is fetch. This way, the
        // cycle count in Binja shows the total instruction cycle count 
        // including the fetch and dispatch cost.
        case mca::HWInstructionEvent::GenericEventType::LastGenericEventType : {
            BridgeRef.CountStore[index].CycleReady = CurrentCycle;
        }
        case mca::HWInstructionEvent::GenericEventType::Executed: {
             BridgeRef.CountStore[index].CycleExecuted = CurrentCycle;
        }
        default: break;
        }
    }

    void onEvent(const mca::HWStallEvent &Event) override {
    }

    void onEvent(const mca::HWPressureEvent &Event) override {
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

    std::unique_ptr<std::thread> ServerThread;
    std::unique_ptr<grpc::Server> server;

    void serverLoop();

    void signalWorkerComplete() override {
        Bridge.IsWaitingForWorker.store(false);
        Bridge.IsWaitingForWorker.notify_all();
    }

    void signalInstructionError(int Index, llvm::Error Err) override {
        Bridge.InstructionErrors.insert(std::make_pair(Index, std::move(Err)));
    }

    int fetch(MutableArrayRef<const MCInst *> MCIS, int Size,
              std::optional<MDExchanger> MDE) override {
        return fetchRegion(MCIS, Size, MDE).first;
    }

    std::pair<int, RegionDescriptor>
    fetchRegion(MutableArrayRef<const MCInst *> MCIS, int Size = -1,
                std::optional<MDExchanger> MDE = std::nullopt) override {

        // Block until new input is available, or shutdown request received
        Bridge.DoneHandlingInput.wait(true);

        if (!Bridge.Running) {
            // shutdown request received; 
            // -1 signals to the worker it is time to shut down.
            return std::make_pair(-1, RegionDescriptor(true));
        }

        if (Size < 0 || Size > MCIS.size()) {
            Size = MCIS.size();
        }

        int num_insn = 0;

        {
            std::lock_guard<std::mutex> Lock(Bridge.QueueMutex);

            num_insn = Bridge.InsnQueue.size();
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
                if (Disassembled == MCDisassembler::DecodeStatus::Fail) {
                  signalInstructionError(
                      i, std::move(createStringError(std::errc::invalid_argument,
                                           "Disassembler reported Fail")));
                  return std::make_pair(i, RegionDescriptor(true));
                }

                MCIS[i] = MCI.get();
                Local_MCIS.push_back(std::move(MCI));

                ++i;
                Bridge.InsnQueue.pop();
            }
        }

        return std::make_pair(num_insn, RegionDescriptor(true));
    }

    unsigned getFeatures() const override {
      return Broker::Feature_Region | Broker::Feature_InstructionError;
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

BinjaBroker::Options::Options() 
    : ListenAddress("0.0.0.0"), ListenPort("50052") {}

BinjaBroker::Options::Options(int argc, const char *const *argv) 
    : BinjaBroker::Options() {
    for (int i = 0; i < argc; ++i) {
        StringRef Arg(argv[i]);

        if (Arg.starts_with("-host") && Arg.contains("=")) {
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
