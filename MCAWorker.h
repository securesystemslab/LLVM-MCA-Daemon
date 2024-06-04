#ifndef MCAD_MCAWORKER_H
#define MCAD_MCAWORKER_H
#include "llvm/Option/Option.h"
#include "llvm/MCA/IncrementalSourceMgr.h"
#include "llvm/MCA/SourceMgr.h"
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
class CustomBehaviour;
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
  const mca::PipelineOptions &MCAPO;
  ToolOutputFile &MCAOF;
  std::unique_ptr<mca::Pipeline> MCAPipeline;
  std::unique_ptr<mca::PipelinePrinter> MCAPipelinePrinter;
  std::set<mca::HWEventListener*> Listeners;

  size_t NumTraceMIs;
  // MCAWorker is the owner of this callback. Note that
  // SummaryView will only take reference of it.
  std::function<size_t(void)> GetTraceMISize;

  llvm::SourceMgr &SM;
  mca::IncrementalSourceMgr SrcMgr;

  mca::CustomBehaviour *CB;

  std::unordered_map<const mca::InstrDesc*,
                     std::set<mca::Instruction*>> RecycledInsts;
  std::function<mca::Instruction*(const mca::InstrDesc&)>
    GetRecycledInst;
  std::function<void(mca::Instruction*)> AddRecycledInst;

  TimerGroup Timers;

  std::unique_ptr<Broker> TheBroker;

  std::unique_ptr<mca::Pipeline> createDefaultPipeline();
  std::unique_ptr<mca::Pipeline> createInOrderPipeline();
  std::unique_ptr<mca::Pipeline> createPipeline();
  void resetPipeline();

  Error runPipeline();

  void printMCA(StringRef RegionDescription = "");

public:
  MCAWorker() = delete;

  MCAWorker(const Target &T, const MCSubtargetInfo &STI, mca::Context &MCA,
            const mca::PipelineOptions &PO, mca::InstrBuilder &IB,
            ToolOutputFile &OF, MCContext &Ctx, const MCAsmInfo &MAI,
            const MCInstrInfo &II, MCInstPrinter &IP, mca::MetadataRegistry &MDR, llvm::SourceMgr &SM);

  BrokerFacade getBrokerFacade() {
    return BrokerFacade(*this);
  }

  Error run();

  ~MCAWorker();
};
} // end namespace mcad
} // end namespace llvm
#endif
