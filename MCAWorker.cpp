#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MCA/Context.h"
#include "llvm/MCA/CustomBehaviour.h"
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
#include "llvm/MCA/Stages/InOrderIssueStage.h"
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

#include "CustomHWUnits/MCADLSUnit.h"
#include "CustomHWUnits/NaiveBranchPredictorUnit.h"
#include "CustomHWUnits/SkylakeBranchUnit.h"
#include "CustomStages/MCADFetchDelayStage.h"
#include "MCAViews/SummaryView.h"
#include "MCAViews/TimelineView.h"
#include "MCAWorker.h"
#include "PipelinePrinter.h"

using namespace llvm;
using namespace mcad;

#define DEBUG_TYPE "llvm-mcad"

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

static cl::opt<bool>
  AssumeNoAlias("noalias",
                cl::desc("If set, assumes that none of the loads and stores alias"),
                cl::init(true));

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

static cl::opt<int>
  MaxNumIdleCycles("max-idle-cycles",
                   cl::desc("Simulate at most N idle cycles in the FetchDelayStage; i.e., if a instruction stalls the pipeline for more than N cycles, do not simulate these cycles. Improves MCAD performance, reduces accuracy. Set to -1 to simulate all cycles."),
                   cl::init(-1));

static cl::opt<unsigned>
  BranchMispredictionDelay("mispredict-delay",
                           cl::desc("Delay (# cycles) added to the fetch stage of the next instruction after a branch misprediction"),
                           cl::init(20U));

static cl::opt<unsigned>
  BranchHistoryTableSize("bht-size",
                         cl::desc("Size of the simulated branch history table for branch prediction"),
                         cl::init(10U));

static cl::opt<bool>
  EnableCache("enable-cache",
                          cl::desc("Simuate the memory cache hierachy"),
                          cl::init(true));

static cl::opt<unsigned>
  NumWays("num-ways",
          cl::desc("Number of ways for all the caches"),
          cl::init(4U));

static cl::opt<unsigned>
  L1ISize("l1i-size",
          cl::desc("Size of the L1 Instruction cache"),
          cl::init(64 * 1024U));

static cl::opt<unsigned>
  L1DSize("l1d-size",
          cl::desc("Size of the L1 Data cache"),
          cl::init(64 * 1024U));

static cl::opt<unsigned>
  L1Latency("l1-latency",
          cl::desc("Latency for accessing the L1 cache"),
          cl::init(3U));

static cl::opt<unsigned>
  L2Size("l2-size",
          cl::desc("Size of the L2 cache"),
          cl::init(512 * 1024U));

static cl::opt<unsigned>
  L2Latency("l2-latency",
          cl::desc("Latency for accessing the L2 cache"),
          cl::init(10U));

static cl::opt<unsigned>
  L3Size("l3-size",
          cl::desc("Size of the L3 cache"),
          cl::init(4 * 1024 * 1024U));

static cl::opt<unsigned>
  L3Latency("l3-latency",
          cl::desc("Latency for accessing the L3 cache"),
          cl::init(30U));

static cl::opt<unsigned>
  MemoryLatency("memory-latency",
          cl::desc("Latency for accessing the main memory"),
          cl::init(300U));

enum BranchPredictor {
  None, Naive, Skylake, IndirectBPU, LocalBPU
};
static cl::opt<BranchPredictor>
  EnableBranchPredictor("enable-branch-predictor",
                        cl::desc("Simuate the branch predictor hardware with one of the given models"),
                        cl::init(BranchPredictor::Naive),
                        cl::values(clEnumVal(None, "Branch predictor is not modeled"),
                                   clEnumVal(Naive, "Naive branch predictor"),
                                   clEnumVal(Skylake, "Predictor modeled after Intel Skylake"))
  );

// FIXME: the way we are keeping these stats is obviously ugly, but let's just
// get something working for now; a more elegant solution would be to use
// custom hardware events and counting those -- this would also allow
// associating these counts with individual instructions
MCADFetchDelayStage::Statistics *FetchDelayStats = nullptr; 
struct CacheStatistics {
  CacheUnit::Statistics *L1IStats = nullptr;
  CacheUnit::Statistics *L1DStats = nullptr;
  CacheUnit::Statistics *L2Stats = nullptr;
  CacheUnit::Statistics *L3Stats = nullptr;
};
struct CacheStatistics CacheStats = {};

namespace {

std::tuple<std::optional<CacheUnit>, std::optional<CacheUnit>> buildCache() {
  if (!EnableCache)
    return std::make_tuple(std::nullopt, std::nullopt);

  auto Memory = std::make_shared<MemoryUnit>(MemoryLatency);
  auto L3 = std::make_shared<CacheUnit>(L3Size, NumWays, Memory, L3Latency);
  auto L2 = std::make_shared<CacheUnit>(L2Size, NumWays, L3, L2Latency);
  CacheUnit L1D(L1DSize, NumWays, L2, L1Latency);
  CacheUnit L1I(L1ISize, NumWays, L2, L1Latency);

  // FIXME -- yes, it's ugly to mix just one set of global statistics in here,
  // but realistically, we're never going to call buildCache() more than once
  // per program execution anayway
  // The L1D and L1S stats need to be assigned after they're copied into their
  // respective hardware units, as they are copied by value, so we'd get a 
  // useless address if we took a pointer here.
  ::CacheStats.L2Stats = &L2->stats;
  ::CacheStats.L3Stats = &L3->stats;

  return std::make_tuple(L1I, L1D);
}

std::unique_ptr<AbstractBranchPredictorUnit> buildBranchPredictor() {
  switch(EnableBranchPredictor) {
    case Naive: {
      auto BPU = std::make_unique<NaiveBranchPredictorUnit>(BranchMispredictionDelay, BranchHistoryTableSize);
      // TODO: Make penalty and history table size command-line parameters
      return BPU;
    }
    case Skylake: {
      auto BPU = std::make_unique<SkylakeBranchUnit>(BranchMispredictionDelay);
      // TODO: Make penalty command-line parameter
      return BPU;
    }
    case None:
    default: 
      return nullptr;
  }
}

} // anonymous namespace

void BrokerFacade::setBroker(std::unique_ptr<Broker> &&B) {
  Worker.TheBroker = std::move(B);
}

void BrokerFacade::registerListener(mca::HWEventListener *EL) {
  Worker.Listeners.insert(EL);
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

SourceMgr &BrokerFacade::getSourceMgr() const { return Worker.SM; }

MCAWorker::MCAWorker(const Target &T, const MCSubtargetInfo &TheSTI,
                     mca::Context &MCA, const mca::PipelineOptions &PO,
                     mca::InstrBuilder &IB, ToolOutputFile &OF, MCContext &C,
                     const MCAsmInfo &AI, const MCInstrInfo &II,
                     MCInstPrinter &IP, MetadataRegistry &MDR,
                     llvm::SourceMgr &SM)
    : TheTarget(T), STI(TheSTI), MCAIB(IB), Ctx(C), MAI(AI), MCII(II), MIP(IP),
      SM(SM), TheMCA(MCA), MCAPO(PO), MCAOF(OF), CB(nullptr), NumTraceMIs(0U),
      GetTraceMISize([this] { return NumTraceMIs; }),
      GetRecycledInst([this](const mca::InstrDesc &Desc) -> mca::Instruction * {
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
      Timers("MCAWorker", "Time consumption in each MCA stages"),
      MDRegistry(MDR) {
  MCAIB.setInstRecycleCallback(GetRecycledInst);
  SrcMgr.setOnInstFreedCallback(AddRecycledInst);

  resetPipeline();
}

std::unique_ptr<mca::Pipeline> MCAWorker::createDefaultPipeline() {
  using namespace mca;
  const MCSchedModel &SM = STI.getSchedModel();
  const MCRegisterInfo &MRI = TheMCA.getMCRegisterInfo();

  if (!SM.isOutOfOrder())
    return createInOrderPipeline();

  // Create the hardware units defining the backend.
  auto RCU = std::make_unique<RetireControlUnit>(SM);
  auto PRF = std::make_unique<RegisterFile>(SM, MRI, MCAPO.RegisterFileSize);
  auto [L1I, L1D] = buildCache();
  auto LSU = std::make_unique<MCADLSUnit>(SM, MCAPO.LoadQueueSize,
                                          MCAPO.StoreQueueSize,
                                          MCAPO.AssumeNoAlias, &MDRegistry, L1D);
  CacheStats.L1DStats = &LSU->CU->stats;
  auto HWS = std::make_unique<Scheduler>(SM, *LSU);
  auto BPU = buildBranchPredictor();

  // Create the pipeline stages.
  auto Fetch = std::make_unique<EntryStage>(SrcMgr);
  auto FetchDelay = std::make_unique<MCADFetchDelayStage>(MCII, MDRegistry, BPU.get(), L1I, MaxNumIdleCycles);
  CacheStats.L1IStats = &FetchDelay->CU->stats;
  FetchDelayStats = &FetchDelay->stats; // TODO: ugly; move this elsewhere using hardware events
  auto Dispatch = std::make_unique<DispatchStage>(STI, MRI, MCAPO.DispatchWidth,
                                                  *RCU, *PRF);
  auto Execute =
      std::make_unique<ExecuteStage>(*HWS, MCAPO.EnableBottleneckAnalysis);
  auto Retire = std::make_unique<RetireStage>(*RCU, *PRF, *LSU);

  // Pass the ownership of all the hardware units to this Context.
  TheMCA.addHardwareUnit(std::move(RCU));
  TheMCA.addHardwareUnit(std::move(PRF));
  TheMCA.addHardwareUnit(std::move(LSU));
  TheMCA.addHardwareUnit(std::move(HWS));
  TheMCA.addHardwareUnit(std::move(BPU));

  // Build the pipeline.
  auto StagePipeline = std::make_unique<Pipeline>();
  StagePipeline->appendStage(std::move(Fetch));
  StagePipeline->appendStage(std::move(FetchDelay));
  if (MCAPO.MicroOpQueueSize)
    StagePipeline->appendStage(std::make_unique<MicroOpQueueStage>(
        MCAPO.MicroOpQueueSize, MCAPO.DecodersThroughput));
  StagePipeline->appendStage(std::move(Dispatch));
  StagePipeline->appendStage(std::move(Execute));
  StagePipeline->appendStage(std::move(Retire));

  for (auto *listener : Listeners) {
    StagePipeline->addEventListener(listener);
  }

  return StagePipeline;
}

std::unique_ptr<mca::Pipeline> MCAWorker::createInOrderPipeline() {
  using namespace mca;
  const MCSchedModel &SM = STI.getSchedModel();
  const MCRegisterInfo &MRI = TheMCA.getMCRegisterInfo();

  auto [L1I, L1D] = buildCache();
  auto PRF = std::make_unique<RegisterFile>(SM, MRI, MCAPO.RegisterFileSize);
  auto LSU = std::make_unique<MCADLSUnit>(SM, MCAPO.LoadQueueSize,
                                          MCAPO.StoreQueueSize,
                                          MCAPO.AssumeNoAlias, &MDRegistry);
  CacheStats.L1DStats = &LSU->CU->stats;
  auto BPU = buildBranchPredictor();

  // Create the pipeline stages.
  auto Entry = std::make_unique<EntryStage>(SrcMgr);
  auto FetchDelay = std::make_unique<MCADFetchDelayStage>(MCII, MDRegistry, BPU.get(), L1I, MaxNumIdleCycles);
  CacheStats.L1IStats = &FetchDelay->CU->stats;
  auto InOrderIssue = std::make_unique<InOrderIssueStage>(STI, *PRF, *CB, *LSU);
  auto StagePipeline = std::make_unique<Pipeline>();

  // Pass the ownership of all the hardware units to this Context.
  TheMCA.addHardwareUnit(std::move(PRF));
  TheMCA.addHardwareUnit(std::move(LSU));
  TheMCA.addHardwareUnit(std::move(BPU));

  // Build the pipeline.
  StagePipeline->appendStage(std::move(Entry));
  StagePipeline->appendStage(std::move(FetchDelay));
  StagePipeline->appendStage(std::move(InOrderIssue));

  for (auto *listener : Listeners) {
    StagePipeline->addEventListener(listener);
  }

  return StagePipeline;
}

std::unique_ptr<mca::Pipeline> MCAWorker::createPipeline() {
  const MCSchedModel &SM = STI.getSchedModel();
  if (SM.isOutOfOrder()) {
    return createDefaultPipeline();
  } else {
    return createInOrderPipeline();
  }
}

void MCAWorker::resetPipeline() {
  RecycledInsts.clear();
  NumTraceMIs = 0U;

  MCAIB.clear();
  SrcMgr.clear();

  if(CB) { delete CB; CB = nullptr; }
  CB = new mca::CustomBehaviour(STI, SrcMgr, MCII);

  MCAPipeline = std::move(createPipeline());
  assert(MCAPipeline);

  MCAPipelinePrinter = std::make_unique<mca::PipelinePrinter>(*MCAPipeline);
  const MCSchedModel &SM = STI.getSchedModel();

  MCAPipelinePrinter->addView(std::make_unique<mca::SummaryView>(
      SM, GetTraceMISize, 0U, &MDRegistry, &MCAOF.os()));

  if (ShowTimelineView)
    MCAPipelinePrinter->addView(
        std::make_unique<mca::TimelineView>(STI, MIP, &MDRegistry, MCAOF.os()));
}

Error MCAWorker::run() {
  if (!TheBroker) {
    return llvm::createStringError(std::errc::invalid_argument,
                                   "No Broker is set");
  }

  const bool UseRegion = TheBroker->hasFeature<Broker::Feature_Region>();
  const bool UseSignalInstructionError = 
    TheBroker->hasFeature<Broker::Feature_InstructionError>();

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

  size_t RegionIdx = 0U;

  bool SupportMetadata = TheBroker->hasFeature<Broker::Feature_Metadata>();
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
          std::tie(Len, RD) = TheBroker->fetchRegion(
              TraceBuffer, -1, MDExchanger{MDRegistry, MDIndexMap});
        } else
          std::tie(Len, RD) = TheBroker->fetchRegion(TraceBuffer);
      } else {
        if (SupportMetadata) {
          MDIndexMap.clear();
          Len = TheBroker->fetch(TraceBuffer, -1,
                                 MDExchanger{MDRegistry, MDIndexMap});
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
            = MCAIB.createInstruction(MCI, {});
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
              if (UseSignalInstructionError) {
                TheBroker->signalInstructionError(i, std::move(RemainingE));
              } else {
                llvm::consumeError(std::move(RemainingE));
              }
              continue;
            }
          }

          // Creating mca::Instruction was successful.
          ++NumTraceMIs;
          if (RecycledInst) {
            if (SupportMetadata && MDIndexMap.count(i)) {
              auto MDTok = MDIndexMap.lookup(i);
              LLVM_DEBUG(dbgs() << "MCI " << NumTraceMIs
                                << " has Token " << MDTok << "\n");
              RecycledInst->setIdentifier(MDTok);
            }
            SrcMgr.addRecycledInst(RecycledInst);
          } else {
            auto &NewInst = InstOrErr.get();
            if (SupportMetadata && MDIndexMap.count(i)) {
              auto MDTok = MDIndexMap.lookup(i);
              LLVM_DEBUG(dbgs() << "MCI " << NumTraceMIs
                                << " has Token " << MDTok << "\n");
              NewInst->setIdentifier(MDTok);
            }

            // Add this instruction to be processed by the pipeline
            // (Entry stage will consume from source manager.)
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

  auto printStat = [&](std::string msg, OverflowableCount c) {
    OS << msg << ": " << c.count << (c.overflowed ? "(overflowed)" : "") << "\n";
  };

  MCAPipelinePrinter->printReport(OS);
  if(FetchDelayStats) {
    printStat("Skipped Idle Cycles", FetchDelayStats->numSkippedDelayCycles);
    printStat("Branch Instructions", FetchDelayStats->numBranches);
    printStat("Branch Mispredictions", FetchDelayStats->numMispredictions);
  }

  if(CacheStats.L1IStats) {
    printStat("L1i Load Misses", CacheStats.L1IStats->numLoadMisses);
    printStat("L1i Load Cycles", CacheStats.L1IStats->numLoadCycles);
    printStat("L1i Store Cycles", CacheStats.L1IStats->numStoreCycles);
  }

  if(CacheStats.L1DStats) {
    printStat("L1d Load Misses", CacheStats.L1DStats->numLoadMisses);
    printStat("L1d Load Cycles", CacheStats.L1DStats->numLoadCycles);
    printStat("L1d Store Cycles", CacheStats.L1DStats->numStoreCycles);
  }

  if(CacheStats.L2Stats) {
    printStat("L2 Load Misses", CacheStats.L2Stats->numLoadMisses);
    printStat("L2 Load Cycles", CacheStats.L2Stats->numLoadCycles);
    printStat("L2 Store Cycles", CacheStats.L2Stats->numStoreCycles);
  }

  if(CacheStats.L3Stats) {
    printStat("L3 Load Misses", CacheStats.L3Stats->numLoadMisses);
    printStat("L3 Load Cycles", CacheStats.L3Stats->numLoadCycles);
    printStat("L3 Store Cycles", CacheStats.L3Stats->numStoreCycles);
  }
}

MCAWorker::~MCAWorker() {
#ifndef NDEBUG
  if (DumpSourceMgrStats)
    SrcMgr.printStatistic(
      dbgs() << "==== IncrementalSourceMgr Stats ====\n");
#endif
}
