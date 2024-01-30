#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MCA/Context.h"
#include "llvm/MCA/HardwareUnits/CacheManager.h"
#include "llvm/MCA/HardwareUnits/RegisterFile.h"
#include "llvm/MCA/HardwareUnits/RetireControlUnit.h"
#include "llvm/MCA/HardwareUnits/Scheduler.h"
#include "llvm/MCA/InstrBuilder.h"
#include "llvm/MCA/Instruction.h"
#include "llvm/MCA/Pipeline.h"
#include "llvm/MCA/Stages/DispatchStage.h"
#include "llvm/MCA/Stages/EntryStage.h"
#include "llvm/MCA/Stages/ExecuteStage.h"
#include "llvm/MCA/Stages/InstructionTables.h"
#include "llvm/MCA/Stages/MicroOpQueueStage.h"
#include "llvm/MCA/Stages/RetireStage.h"
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
#include <iostream>
#include <unistd.h>

#include "MCAWorker.h"
#include "MCAViews/SummaryView.h"
#include "MCAViews/TimelineView.h"
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

static cl::opt<bool>
  PreserveReturnInst("use-return-inst",
                   cl::desc("Include return instruction in MCA"),
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
static cl::opt<bool>
  UseLoadLatency("mca-use-load-latency",
                 cl::desc("Use `MCSchedModel::LoadLatency` to "
                          "model load instructions"),
                 cl::init(true));

// TODO: Put this into a separate CL option group
static cl::opt<bool>
  ShowTimelineView("mca-show-timeline-view",
                   cl::init(false));

void BrokerFacade::setBroker(std::unique_ptr<Broker> &&B) {
  Worker.TheBroker = std::move(B);
}

void BrokerFacade::registerListener(mca::HWEventListener *EL) {
  Worker.MCAPipeline->addEventListener(EL);
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

MCAWorker::MCAWorker(const Target &T,
                     const MCSubtargetInfo &TheSTI,
                     mca::Context &MCA,
                     const mca::PipelineOptions &PO,
                     mca::InstrBuilder &IB,
                     ToolOutputFile &OF,
                     MCContext &C,
                     const MCAsmInfo &AI,
                     const MCInstrInfo &II,
                     MCInstPrinter &IP)
  : TheTarget(T), STI(TheSTI),
    MCAIB(IB), Ctx(C), MAI(AI), MCII(II), MIP(IP),
    TheMCA(MCA), MCAPO(PO), MCAOF(OF),
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

  MCAIB.useLoadLatency(UseLoadLatency);

  resetPipeline();
}

std::unique_ptr<mca::Pipeline> MCAWorker::createPipeline() {
  using namespace mca;
  const MCSchedModel &SM = STI.getSchedModel();
  const MCRegisterInfo &MRI = TheMCA.getMCRegisterInfo();

  // Create the hardware units defining the backend.
  auto RCU = std::make_unique<RetireControlUnit>(SM);
  auto PRF = std::make_unique<RegisterFile>(SM, MRI, MCAPO.RegisterFileSize);
  auto LSU = std::make_unique<LSUnit>(SM, MCAPO.LoadQueueSize,
                                       MCAPO.StoreQueueSize, MCAPO.AssumeNoAlias,
                                       TheMCA.getMetadataRegistry());
  std::unique_ptr<CacheManager> HWC;
  if (CacheConfigFile.size() && TheMCA.getMetadataRegistry())
    HWC = std::make_unique<CacheManager>(CacheConfigFile,
                                         *TheMCA.getMetadataRegistry());
  auto HWS = std::make_unique<Scheduler>(SM, *LSU, HWC.get());

  // Create the pipeline stages.
  auto Fetch = std::make_unique<EntryStage>(SrcMgr, STI, TheMCA.getMetadataRegistry());
  auto Dispatch = std::make_unique<DispatchStage>(STI, MRI, MCAPO.DispatchWidth,
                                                   *RCU, *PRF);
  auto Execute =
      std::make_unique<ExecuteStage>(*HWS, MCAPO.EnableBottleneckAnalysis);
  auto Retire = std::make_unique<RetireStage>(*RCU, *PRF, *LSU);

  // Pass the ownership of all the hardware units to this Context.
  TheMCA.addHardwareUnit(std::move(RCU));
  TheMCA.addHardwareUnit(std::move(PRF));
  TheMCA.addHardwareUnit(std::move(LSU));
  if (HWC)
    TheMCA.addHardwareUnit(std::move(HWC));
  TheMCA.addHardwareUnit(std::move(HWS));

  // Build the pipeline.
  auto StagePipeline = std::make_unique<Pipeline>();
  StagePipeline->appendStage(std::move(Fetch));
  if (MCAPO.MicroOpQueueSize)
    StagePipeline->appendStage(std::make_unique<MicroOpQueueStage>(
        MCAPO.MicroOpQueueSize, MCAPO.DecodersThroughput));
  StagePipeline->appendStage(std::move(Dispatch));
  StagePipeline->appendStage(std::move(Execute));
  StagePipeline->appendStage(std::move(Retire));
  return StagePipeline;
}

void MCAWorker::resetPipeline() {
  RecycledInsts.clear();
  NumTraceMIs = 0U;

  MCAIB.clear();
  SrcMgr.clear();

  MCAPipeline = createPipeline();
  assert(MCAPipeline);

  MCAPipelinePrinter
    = std::make_unique<mca::PipelinePrinter>(*MCAPipeline,
                                             PrintJson ? mca::View::OK_JSON
                                                       : mca::View::OK_READABLE);
  const MCSchedModel &SM = STI.getSchedModel();
  MCAPipelinePrinter->addView(
    std::make_unique<mca::SummaryView>(SM, GetTraceMISize, 0U,
                                       TheMCA.getMetadataRegistry(),
                                       &MCAOF.os()));
  if (ShowTimelineView)
    MCAPipelinePrinter->addView(
      std::make_unique<mca::TimelineView>(STI, MIP,
                                          *TheMCA.getMetadataRegistry(),
                                          MCAOF.os()));
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

  mca::MetadataRegistry *MDRegistry = TheMCA.getMetadataRegistry();
  bool SupportMetadata = TheBroker->hasFeature<Broker::Feature_Metadata>();
  assert((!SupportMetadata || MDRegistry) &&
         "MetadataRegistry not created?");
  DenseMap<unsigned, unsigned> MDIndexMap;

  // The end of instruction streams in all regions
  bool EndOfStream = false;
  while (true) {
    bool Continue = true;
    Broker::RegionDescriptor RD(/*IsEnd=*/false);
    while (Continue) {
      int Len = 0;
      if (UseRegion) {
        if (SupportMetadata) {
          MDIndexMap.clear();
          std::tie(Len, RD)
            = TheBroker->fetchRegion(TraceBuffer, -1,
                                     MDExchanger{*MDRegistry, MDIndexMap});
        } else
          std::tie(Len, RD) = TheBroker->fetchRegion(TraceBuffer);
      } else {
        if (SupportMetadata) {
          MDIndexMap.clear();
          Len = TheBroker->fetch(TraceBuffer, -1,
                                 MDExchanger{*MDRegistry, MDIndexMap});
        } else
          Len = TheBroker->fetch(TraceBuffer);
      }

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
          // not really meaningful.
          if (!PreserveReturnInst)
            if (MCID.isReturn())
              continue;

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
            if (SupportMetadata && MDIndexMap.count(i)) {
              auto MDTok = MDIndexMap.lookup(i);
              LLVM_DEBUG(dbgs() << "MCI " << NumTraceMIs
                                << " has Token " << MDTok << "\n");
              RecycledInst->setMetadataToken(MDTok);
            }
            SrcMgr.addRecycledInst(RecycledInst);
          } else {
            auto &NewInst = InstOrErr.get();
            if (SupportMetadata && MDIndexMap.count(i)) {
              auto MDTok = MDIndexMap.lookup(i);
              LLVM_DEBUG(dbgs() << "MCI " << NumTraceMIs
                                << " has Token " << MDTok << "\n");
              NewInst->setMetadataToken(MDTok);
            }
            SrcMgr.addInst(std::move(NewInst));
          }
        }
      }

      if (NumTraceMIs) {
        if (auto E = runPipeline())
          return E;
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
    
    TheBroker->signalWorkerComplete();

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
