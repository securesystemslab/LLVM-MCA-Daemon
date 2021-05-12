#ifndef MCAD_MCAWORKER_H
#define MCAD_MCAWORKER_H
#include "llvm/ADT/Optional.h"
#include "llvm/MCA/SourceMgr.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Timer.h"
#include <functional>
#include <utility>
#include <list>
#include <unordered_map>
#include <set>

#include "Brokers/Broker.h"

namespace llvm {
class ToolOutputFile;
class MCContext;
class MCSubtargetInfo;
class MCInst;
class MCInstPrinter;
class MCInstrInfo;
namespace mca {
class Context;
class InstrBuilder;
class Pipeline;
class PipelineOptions;
class PipelinePrinter;
class InstrDesc;
class Instruction;
} // end namespace mca

namespace mcad {
class MCAWorker {
  const MCSubtargetInfo &STI;
  mca::InstrBuilder &MCAIB;
  const MCInstrInfo &MCII;
  MCInstPrinter &MIP;
  std::unique_ptr<mca::Pipeline> MCAPipeline;
  std::unique_ptr<mca::PipelinePrinter> MCAPipelinePrinter;

  std::list<const MCInst*> TraceMIs;
  // MCAWorker is the owner of this callback. Note that
  // SummaryView will only take reference of it.
  std::function<size_t(void)> GetTraceMISize;

  mca::IncrementalSourceMgr SrcMgr;

  std::unordered_map<const mca::InstrDesc*,
                     std::set<mca::Instruction*>> RecycledInsts;
  std::function<mca::Instruction*(const mca::InstrDesc&)>
    GetRecycledInst;
  std::function<void(mca::Instruction*)> AddRecycledInst;

  TimerGroup Timers;

  Optional<Broker> TheBroker;

  Error runPipeline();

public:
  MCAWorker() = delete;

  MCAWorker(const MCSubtargetInfo &STI,
            mca::Context &MCA,
            const mca::PipelineOptions &PO,
            mca::InstrBuilder &IB,
            const MCInstrInfo &II,
            MCInstPrinter &IP);

  // An interface that provides objects that might be needed
  // to build a Broker. It's also the interface to register a
  // Broker.
  // This class is trivially-copyable
  class BrokerFacade {
    friend class MCAWorker;
    MCAWorker &Worker;

    explicit BrokerFacade(MCAWorker &W) : Worker(W) {}

  public:
    void setBroker(Broker &&B) {
      Worker.TheBroker = std::move(B);
    }

    const MCSubtargetInfo &getSTI() const {
      return Worker.STI;
    }
  };
  BrokerFacade getBrokerFacade() {
    return BrokerFacade(*this);
  }

  Error run();

  void printMCA(ToolOutputFile &OF);
};
} // end namespace mcad
} // end namespace llvm
#endif
