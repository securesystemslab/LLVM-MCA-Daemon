#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
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
#include <system_error>

#include "MCAWorker.h"
#include "MCAViews/SummaryView.h"
#include "PipelinePrinter.h"

using namespace llvm;
using namespace mcad;

#define DEBUG_TYPE "llvm-mcad"

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

#define DEFAULT_MAX_NUM_PROCESSED 1000U
static cl::opt<unsigned>
  MaxNumProcessedInst("mca-max-chunk-size",
                      cl::desc("Max number of instructions processed at a time"),
                      cl::init(DEFAULT_MAX_NUM_PROCESSED));
#ifndef NDEBUG
static cl::opt<bool>
  DumpSourceMgrStats("dump-mca-sourcemgr-stats",
                     cl::Hidden, cl::init(false));
#endif

static cl::opt<unsigned>
  NumMCAIterations("mca-iteration",
                   cl::desc("Number of MCA simulation iteration"),
                   cl::init(1U));

static cl::opt<std::string>
  CacheConfigFile("cache-sim-config",
                  cl::desc("Path to config file for cache simulation"),
                  cl::Hidden);

void BrokerFacade::setBroker(std::unique_ptr<Broker> &&B) {
  Worker.TheBroker = std::move(B);
}

const Target &BrokerFacade::getTarget() const {
  return Worker.TheTarget;
}

MCContext &BrokerFacade::getCtx() const {
  return Worker.Ctx;
}

const MCAsmInfo &BrokerFacade::getAsmInfo() const {
  return Worker.MAI;
}

const MCInstrInfo &BrokerFacade::getInstrInfo() const {
  return Worker.MCII;
}

const MCSubtargetInfo &BrokerFacade::getSTI() const {
  return Worker.STI;
}

SourceMgr &BrokerFacade::getSourceMgr() const {
  return Worker.SM;
}

MCAWorker::MCAWorker(const Target &T,
                     const MCSubtargetInfo &TheSTI,
                     mca::Context &MCA,
                     const mca::PipelineOptions &PO,
                     mca::InstrBuilder &IB,
                     ToolOutputFile &OF,
                     MCContext &C,
                     const MCAsmInfo &AI,
                     const MCInstrInfo &II,
                     MCInstPrinter &IP,
                     mca::MetadataRegistry &MDR,
                     llvm::SourceMgr &SM)
  : TheTarget(T), STI(TheSTI),
    MCAIB(IB), Ctx(C), MAI(AI), MCII(II), MIP(IP), MDR(MDR),
    TheMCA(MCA), MCAPO(PO), MCAOF(OF), CB(nullptr),
    NumTraceMIs(0U), GetTraceMISize([this]{ return NumTraceMIs; }),
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
    Timers("MCAWorker", "Time consumption in each MCA stages") {
  MCAIB.setInstRecycleCallback(GetRecycledInst);
  SrcMgr.setOnInstFreedCallback(AddRecycledInst);

  resetPipeline();
}


void MCAWorker::resetPipeline() {
  RecycledInsts.clear();
  NumTraceMIs = 0U;

  MCAIB.clear();
  SrcMgr.clear();

  if(CB) { delete CB; CB = nullptr; }
  CB = new mca::CustomBehaviour(STI, SrcMgr, MCII);
  MCAPipeline = std::move(TheMCA.createDefaultPipeline(MCAPO, SrcMgr, *CB));
  assert(MCAPipeline);

  MCAPipelinePrinter
    = std::make_unique<mca::PipelinePrinter>(*MCAPipeline);
  const MCSchedModel &SM = STI.getSchedModel();
  MCAPipelinePrinter->addView(
    std::make_unique<mca::SummaryView>(SM, GetTraceMISize, 0U,
                                       &MDR,
                                       &MCAOF.os()));
}

Error MCAWorker::run() {
  if (!TheBroker) {
    return llvm::createStringError(std::errc::invalid_argument,
                                   "No Broker is set");
  }

  raw_ostream *TraceOS = nullptr;
  std::unique_ptr<ToolOutputFile> TraceTOF;
  if (TraceMCI) {
    std::error_code EC;
    TraceTOF
      = std::make_unique<ToolOutputFile>(MCITraceFile, EC, sys::fs::OF_Text);
    if (EC) {
      errs() << "Failed to open trace file: " << EC.message() << "\n";
    } else {
      TraceOS = &TraceTOF->os();
    }

    // Call ToolOutputFile::keep as early as possible s.t. if anything goes
    // wrong later we still have the trace file.
    if (TraceTOF)
      TraceTOF->keep();
  }

  SmallVector<const MCInst*, DEFAULT_MAX_NUM_PROCESSED>
    TraceBuffer(MaxNumProcessedInst);

  bool UseRegion = TheBroker->hasFeature<Broker::Feature_Region>();
  size_t RegionIdx = 0U;

  DenseMap<unsigned, unsigned> MDIndexMap;

  // The end of instruction streams in all regions
  bool EndOfStream = false;
  while (true) {
    bool Continue = true;
    Broker::RegionDescriptor RD(/*IsEnd=*/false);
    while (Continue) {
      int Len = 0;
      if (UseRegion) {
         std::tie(Len, RD) = TheBroker->fetchRegion(TraceBuffer);
      } else
         Len = TheBroker->fetch(TraceBuffer);

      if (Len < 0 || RD) {
        SrcMgr.endOfStream();
        Continue = false;
        if (Len < 0) {
          Len = 0;
          EndOfStream = true;
        }
      }

      ArrayRef<const MCInst*> TraceBufferSlice(TraceBuffer);
      TraceBufferSlice = TraceBufferSlice.take_front(Len);

      static Timer TheTimer("MCAInstrBuild", "MCA Build Instruction", Timers);
      {
        TimeRegion TR(TheTimer);

        // Convert MCInst to mca::Instruction
        for (unsigned i = 0U, S = TraceBufferSlice.size();
             i < S; ++i) {
          const MCInst &MCI = *TraceBufferSlice[i];
          ++NumTraceMIs;
          const auto &MCID = MCII.get(MCI.getOpcode());
          // Always ignore return instruction since it's
          // not really meaningful
          if (MCID.isReturn()) continue;
          if (!PreserveCallInst)
            if (MCID.isCall())
              continue;

          if (TraceOS) {
            MIP.printInst(&MCI, 0, "", STI, *TraceOS);
            (*TraceOS) << "\n";
          }

          mca::Instruction *RecycledInst = nullptr;
          Expected<std::unique_ptr<mca::Instruction>> InstOrErr
            = MCAIB.createInstruction(MCI);
          if (!InstOrErr) {
            if (auto RemainingE = handleErrors(
                     InstOrErr.takeError(),
                     [&](const mca::RecycledInstErr &RC) {
                       RecycledInst = RC.getInst();
                     })) {
#if 0
              llvm::logAllUnhandledErrors(std::move(RemainingE),
                                          WithColor::error());
              MIP.printInst(&MCI, 0, "", STI,
                            WithColor::note() << "Current MCInst: ");
              errs() << "\n";
#endif
              // FIXME: Ideally we should print out the error in this
              // stage before carrying on, just like the commented code above.
              // But we are seeing tremendous number of errors caused by the
              // lack of MCSched info for 'hint X' instructions in AArch64.
              // And these error messages will actually overflow our python
              // harness used in the experiments :-P Thus we're temporarily
              // disabling the error message here.
              llvm::consumeError(std::move(RemainingE));
              continue;
            }
          }
          if (RecycledInst) {
            SrcMgr.addRecycledInst(RecycledInst);
          } else {
            auto &NewInst = InstOrErr.get();
            SrcMgr.addInst(std::move(NewInst));
          }
        }
      }

      if (NumTraceMIs) {
        if (auto E = runPipeline()) {
          delete CB;
          CB = nullptr;
          return E;
        }
      }
    }
    if (UseRegion) {
      if (!RD.Description.empty())
        printMCA(RD.Description);
      else
        printMCA(std::string("Region [") +
                 std::to_string(RegionIdx++) +
                 std::string(1, ']'));
    } else
      printMCA();

    if (EndOfStream)
      break;
    if (UseRegion) {
      resetPipeline();
      if (TraceOS) {
        (*TraceOS) << MAI.getCommentString()
                   << " === End Of Region ===\n";
      }
    }
  }

  delete CB;
  CB = nullptr;

  return ErrorSuccess();
}

Error MCAWorker::runPipeline() {
  assert(MCAPipeline);
  static Timer TheTimer("RunMCAPipeline", "MCA Pipeline", Timers);
  TimeRegion TR(TheTimer);

  Expected<unsigned> Cycles = MCAPipeline->run();
  if (!Cycles) {
    if (!Cycles.errorIsA<mca::InstStreamPause>()) {
      return Cycles.takeError();
    } else {
      // Consume the error
      handleAllErrors(std::move(Cycles.takeError()),
                      [](const mca::InstStreamPause &PE) {});
    }
  }

  return ErrorSuccess();
}

void MCAWorker::printMCA(StringRef RegionDescription) {
  if (!NumTraceMIs) return;

  raw_ostream &OS = MCAOF.os();
  // Print region description text if feasible
  if (!RegionDescription.empty())
    OS << "\n=== Printing report for "
       << RegionDescription << " ===\n";

  MCAPipelinePrinter->printReport(OS);
}

MCAWorker::~MCAWorker() {
#ifndef NDEBUG
  if (DumpSourceMgrStats)
    SrcMgr.printStatistic(
      dbgs() << "==== IncrementalSourceMgr Stats ====\n");
#endif
}
