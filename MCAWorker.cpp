#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MCA/Context.h"
#include "llvm/MCA/InstrBuilder.h"
#include "llvm/MCA/Instruction.h"
#include "llvm/MCA/Pipeline.h"
#include "llvm/MCA/Stages/EntryStage.h"
#include "llvm/MCA/Stages/InstructionTables.h"
#include "llvm/MCA/Support.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/WithColor.h"
#include <string>

#include "MCAWorker.h"
#include "MCAViews/SummaryView.h"
#include "PipelinePrinter.h"

using namespace llvm;
using namespace mcad;

static cl::opt<bool>
  PrintJson("print-json", cl::desc("Export MCA analysis in JSON format"),
            cl::init(false));

static cl::opt<bool>
  TraceMCI("dump-trace-mc-inst", cl::desc("Dump collected MCInst in the trace"),
           cl::init(false));
static cl::opt<std::string>
  MCITraceFile("trace-mc-inst-output",
               cl::desc("Output to file for `-dump-trace-mc-inst`"
                        ". Print them to stdout otherwise"),
               cl::init("-"));

static cl::opt<bool>
  PreserveCallInst("use-call-inst",
                   cl::desc("Include call instruction in MCA"),
                   cl::init(false));

static cl::opt<unsigned>
  MaxNumProcessedInst("mca-max-chunk-size",
                      cl::desc("Max number of instructions processed at a time"),
                      cl::init(10000U));
#ifndef NDEBUG
static cl::opt<bool>
  DumpSourceMgrStats("dump-mca-sourcemgr-stats",
                     cl::Hidden, cl::init(false));
#endif

static cl::opt<unsigned>
  NumMCAIterations("mca-iteration",
                   cl::desc("Number of MCA simulation iteration"),
                   cl::init(1U));

MCAWorker::MCAWorker(const MCSubtargetInfo &TheSTI,
                     mca::Context &MCA,
                     const mca::PipelineOptions &PO,
                     mca::InstrBuilder &IB,
                     const MCInstPrinter &IP)
  : STI(TheSTI), MCAIB(IB), MIP(IP),
    TraceMIs(), GetTraceMISize([this]{ return TraceMIs.size(); }),
    GetRecycledInst([this](const mca::InstrDesc &Desc) -> mca::Instruction* {
                      if (RecycledInsts.count(&Desc)) {
                        auto &Insts = RecycledInsts[&Desc];
                        if (Insts.size()) {
                          mca::Instruction *I = *Insts.begin();
                          Insts.erase(Insts.cbegin());
                          return I;
                        }
                      }
                      return nullptr;
                    }),
    AddRecycledInst([this](mca::Instruction *I) {
                      const mca::InstrDesc &D = I->getDesc();
                      RecycledInsts[&D].insert(I);
                    }),
    Timers("MCABridge", "Time consumption in each MCABridge stages") {
  MCAIB.setInstRecycleCallback(GetRecycledInst);

  SrcMgr.setOnInstFreedCallback(AddRecycledInst);

  MCAPipeline = std::move(MCA.createDefaultPipeline(PO, SrcMgr));
  assert(MCAPipeline);

  MCAPipelinePrinter
    = std::make_unique<mca::PipelinePrinter>(*MCAPipeline,
                                             PrintJson ? mca::View::OK_JSON
                                                       : mca::View::OK_READABLE);
  const MCSchedModel &SM = STI.getSchedModel();
  MCAPipelinePrinter->addView(
    std::make_unique<mca::SummaryView>(SM, GetTraceMISize, 0U));
}


