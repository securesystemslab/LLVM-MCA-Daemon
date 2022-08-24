#ifndef MCAD_MCAWORKER_H
#define MCAD_MCAWORKER_H
#include "llvm/ADT/Optional.h"
#include "llvm/MCA/IncrementalSourceMgr.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Timer.h"
#include <functional>
#include <utility>
#include <list>
#include <unordered_map>
#include <set>

#include "BrokerFacade.h"
#include "Brokers/Broker.h"

namespace llvm {
class Target;
class ToolOutputFile;
class MCAsmInfo;
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
  friend class BrokerFacade;
  const Target &TheTarget;
  const MCSubtargetInfo &STI;
  mca::InstrBuilder &MCAIB;
  MCContext &Ctx;
  const MCAsmInfo &MAI;
  const MCInstrInfo &MCII;
  MCInstPrinter &MIP;
  mca::Context &TheMCA;
  mca::MetadataRegistry &MDR;
  const mca::PipelineOptions &MCAPO;
  ToolOutputFile &MCAOF;
  std::unique_ptr<mca::Pipeline> MCAPipeline;
  std::unique_ptr<mca::PipelinePrinter> MCAPipelinePrinter;

  size_t NumTraceMIs;
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

  std::unique_ptr<Broker> TheBroker;

  void resetPipeline();

  Error runPipeline();

  void printMCA(StringRef RegionDescription = "");

public:
  MCAWorker() = delete;

  MCAWorker(const Target &T,
            const MCSubtargetInfo &STI,
            mca::Context &MCA,
            const mca::PipelineOptions &PO,
            mca::InstrBuilder &IB,
            ToolOutputFile &OF,
            MCContext &Ctx,
            const MCAsmInfo &MAI,
            const MCInstrInfo &II,
            MCInstPrinter &IP,
            mca::MetadataRegistry &MDR
            );

  BrokerFacade getBrokerFacade() {
    return BrokerFacade(*this);
  }

  Error run();

  ~MCAWorker();
};
} // end namespace mcad
} // end namespace llvm
#endif
