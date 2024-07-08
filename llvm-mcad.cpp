#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/MCA/Context.h"
#include "llvm/MCA/InstrBuilder.h"
#include "llvm/MCA/Pipeline.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include <cstdlib>
#include <string>

#include "Brokers/AsmFileBroker.h"
#include "Brokers/BrokerPlugin.h"
#include "MCAWorker.h"
#include "PipelinePrinter.h"

#ifdef LLVM_MCAD_ENABLE_TCMALLOC
#include "gperftools/heap-profiler.h"
#endif
#ifdef LLVM_MCAD_ENABLE_PROFILER
#include "gperftools/profiler.h"
#endif
# if defined(LLVM_MCAD_ENABLE_PROFILER) || defined(LLVM_MCAD_ENABLE_TCMALLOC)
#include <signal.h>
#endif

using namespace llvm;
using namespace mcad;

#define DEBUG_TYPE  "llvm-mcad"

static mc::RegisterMCTargetOptionsFlags MOF;

static cl::OptionCategory CoreOptionsCat("MCAD Core Options");

static cl::opt<std::string>
  TripleName("mtriple", cl::desc("Target triple to use"),
             cl::cat(CoreOptionsCat));

static cl::opt<std::string>
  ArchName("march", cl::desc("Target architecture"),
           cl::cat(CoreOptionsCat));

static cl::opt<std::string>
  CPUName("mcpu", cl::value_desc("cpu-name"),
          cl::desc("Specific CPU to use (i.e. `-mcpu`)"),
          cl::init("native"),
          cl::cat(CoreOptionsCat));

static cl::opt<std::string>
  MAttr("mattr", cl::desc("Additional target feature"),
        cl::cat(CoreOptionsCat));

enum BrokerType {
  // Builtin brokers
  BT_AsmFile,  // Read from assembly file
  BT_RawBytes, // Read raw bytes instructiions from socket

  BT_Plugin    // Use broker plugin
};
static cl::opt<BrokerType>
  UseBroker("broker", cl::desc("Select the broker to use"),
            cl::values(
              clEnumValN(BT_AsmFile,  "asm",    "Read from assembly file"),
              clEnumValN(BT_RawBytes, "raw",    "Raw instructions via socket"),
              clEnumValN(BT_Plugin,   "plugin", "Use plugin")
            ),
            cl::init(BT_AsmFile),
            cl::cat(CoreOptionsCat));

// This will replace UseBroker with BT_Plugin
static cl::opt<std::string>
  BrokerPluginPath("load-broker-plugin",
                   cl::desc("Load broker plugin from <path>"),
                   cl::value_desc("path"),
                   cl::cat(CoreOptionsCat));

// For example: `-broker-plugin-arg-foo=hello` will pass `-foo=hello`
// to the plugin
static cl::list<std::string>
  BrokerPluginArg("broker-plugin-arg", cl::AlwaysPrefix,
                  cl::desc("Argument passed to broker plugin"),
                  cl::cat(CoreOptionsCat));

static cl::opt<std::string>
  OutputFilename("mca-output", cl::desc("Path to export MCA analysis"),
                 cl::init("-"),
                 cl::cat(CoreOptionsCat));

static cl::opt<std::string>
  AddressFilterFile("addr-filter-file",
                    cl::desc("Only analyze address ranges described "
                                   "in this list"),
                    cl::Hidden,
                    cl::cat(CoreOptionsCat));

static cl::opt<bool>
  EnableTimer("enable-timer", cl::desc("Print timing breakdown of each components"),
              cl::init(false), cl::Hidden);

#ifdef LLVM_MCAD_ENABLE_TCMALLOC
static cl::opt<bool>
  EnableHeapProfile("heap-profile",
                    cl::desc("Profiling heap usage"),
                    cl::init(false), cl::Hidden);
static cl::opt<std::string>
  HeapProfileOutputPath("heap-profile-output",
                        cl::desc("Output path for the heap profiler"),
                        cl::init(""), cl::Hidden);
#endif
#ifdef LLVM_MCAD_ENABLE_PROFILER
static cl::opt<bool>
  EnableCpuProfile("cpu-profile",
                   cl::desc("Profiling CPU usage"),
                   cl::init(false), cl::Hidden);
static cl::opt<std::string>
  CpuProfileOutputPath("cpu-profile-output",
                        cl::desc("Output path for the CPU profiler"),
                        cl::init(""), cl::Hidden);
#endif

static const llvm::Target *getLLVMTarget(const char *ProgName) {
  using namespace llvm;
  if (TripleName.empty())
    TripleName = Triple::normalize(sys::getDefaultTargetTriple());
  Triple TheTriple(TripleName);

  std::string Error;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(ArchName, TheTriple, Error);
  if (!TheTarget) {
    WithColor::error() << ProgName << ": " << Error;
    return nullptr;
  }

  // TargetRegistry::lookupTarget might adjust the triple
  // according to ArchName
  TripleName = TheTriple.getTriple();

  // Return the found target.
  return TheTarget;
}

#if defined(LLVM_MCAD_ENABLE_PROFILER) || defined(LLVM_MCAD_ENABLE_TCMALLOC)
namespace {
static struct {
  int CpuProfilerSignal;
  int HeapProfilerSignal;

  unsigned CpuProfilerStarted : 1;
  unsigned HeapProfilerStarted : 1;

  void toggleCpuProfiler() {
#ifdef LLVM_MCAD_ENABLE_PROFILER
    if (CpuProfilerStarted)
      ProfilerStop();
    else {
      if (!ProfilerStart(CpuProfileOutputPath.c_str()))
        WithColor::error() << "Failed to turn on CPU profiler\n";
      else
        WithColor::note() << "CPU profiler is running...\n";
    }
    CpuProfilerStarted = !CpuProfilerStarted;
#endif
  }

  void toggleHeapProfiler() {
#ifdef LLVM_MCAD_ENABLE_TCMALLOC
    if (HeapProfilerStarted)
      HeapProfilerStop();
    else
      HeapProfilerStart(HeapProfileOutputPath.c_str());

    HeapProfilerStarted = !HeapProfilerStarted;
#endif
  }
} ProfilersManager{-1, -1, false, false};
} // end anonymous namespace
#endif

#if defined(LLVM_MCAD_ENABLE_PROFILER) || defined(LLVM_MCAD_ENABLE_TCMALLOC)
static void profilersSwitch(int SignalNumber) {
  if (SignalNumber == ProfilersManager.CpuProfilerSignal)
    ProfilersManager.toggleCpuProfiler();
  if (SignalNumber == ProfilersManager.HeapProfilerSignal)
    ProfilersManager.toggleHeapProfiler();
}
#endif

static inline int initializeProfilers() {
#ifdef LLVM_MCAD_ENABLE_TCMALLOC
  if (EnableHeapProfile) {
    if (HeapProfileOutputPath.empty()) {
      SmallString<20> TmpPath;
      auto EC = llvm::sys::fs::createTemporaryFile("tmp_heap_profile", "hp",
                                                   TmpPath);
      if (EC) {
        errs() << "Failed to create temporary heap profile file: "
               << EC.message() << "\n";
        return 1;
      }
      HeapProfileOutputPath = (std::string)TmpPath;
    }
    auto *ProfilerSignal = ::getenv("HEAPPROFILESIGNAL");
    if (ProfilerSignal) {
      auto SignalNum = ::atol(ProfilerSignal);
      if (SignalNum >= 1 && SignalNum <= 64) {
        ProfilersManager.HeapProfilerSignal = SignalNum;
        if (::signal(SignalNum, profilersSwitch) == SIG_ERR)
          WithColor::error() << "Fail to setup handler for signal "
                             << SignalNum << "\n";
        else
          WithColor::note() << "Using signal " << SignalNum
                            << " as heap profiler switch\n";
      } else {
        WithColor::error() << "Invalid heap profiler signal "
                           << SignalNum << "\n";
      }
    } else {
      ProfilersManager.toggleHeapProfiler();
    }
  }
#endif

#ifdef LLVM_MCAD_ENABLE_PROFILER
  if (EnableCpuProfile) {
    if (CpuProfileOutputPath.empty()) {
      SmallString<20> TmpPath;
      auto EC = llvm::sys::fs::createTemporaryFile("tmp_cpu_profile", "prof",
                                                   TmpPath);
      if (EC) {
        errs() << "Failed to create temporary CPU profile file: "
               << EC.message() << "\n";
        return 1;
      }
      CpuProfileOutputPath = (std::string)TmpPath;
    }
    auto *ProfilerSignal = ::getenv("CPUPROFILESIGNAL");
    if (ProfilerSignal) {
      auto SignalNum = ::atol(ProfilerSignal);
      if (SignalNum >= 1 && SignalNum <= 64) {
        ProfilersManager.CpuProfilerSignal = SignalNum;
        if (::signal(SignalNum, profilersSwitch) == SIG_ERR)
          WithColor::error() << "Fail to setup handler for signal "
                             << SignalNum << "\n";
        else
          WithColor::note() << "Using signal " << SignalNum
                            << " as CPU profiler switch\n";
      } else {
        WithColor::error() << "Invalid CPU profiler signal "
                           << SignalNum << "\n";
      }
    } else {
      ProfilersManager.toggleCpuProfiler();
    }
  }
#endif
  return 0;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  // Initialize targets
  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  // Although `llvm-mcad` doesn't use the disassembler, some broker
  // plugin might use it. However, if we call InitializeAllDisassemblers
  // in the broker plugin, it will fail to retrieve the correct `Target`
  // (singletone) instance in the case of linking LLVM libraries statically.
  // Therefore, we have little choises but initializing them here.
  // FIXME: Put "require disassembler" as a Broker feature
  // and initialize them only when that feature is given.
  InitializeAllDisassemblers();

  // Print all available targets in `--verison`
  cl::AddExtraVersionPrinter(TargetRegistry::printRegisteredTargetsForVersion);

  cl::ParseCommandLineOptions(argc, argv, "LLVM MCA Daemon");

  // Initializing the MC components we need
  const Target *TheTarget = getLLVMTarget(argv[0]);
  if (!TheTarget)
    return 1;

  // GetTarget() may replaced TripleName with a default triple.
  // For safety, reconstruct the Triple object.
  Triple TheTriple(TripleName);

  if (CPUName == "native")
    CPUName = std::string(sys::getHostCPUName());

  std::unique_ptr<MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(TripleName, CPUName, MAttr));
  assert(STI && "Unable to create subtarget info!");
  if (!STI->isCPUStringValid(CPUName))
    return 1;

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TripleName));
  assert(MRI);
  MCTargetOptions MCOptions = mc::InitMCTargetOptionsFromFlags();
    std::unique_ptr<MCAsmInfo> MAI(
            TheTarget->createMCAsmInfo(*MRI, TripleName, MCOptions));
  assert(MAI);

  auto Ctx = std::make_unique<MCContext>(TheTriple,
                                         MAI.get(), MRI.get(), STI.get());
  std::unique_ptr<MCObjectFileInfo> MOFI(
    TheTarget->createMCObjectFileInfo(*Ctx, /*PIC=*/false));
  Ctx->setObjectFileInfo(MOFI.get());

  std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
  assert(MCII && "Unable to create instruction info!");

  std::unique_ptr<MCInstrAnalysis> MCIA(
      TheTarget->createMCInstrAnalysis(MCII.get()));

  std::unique_ptr<MCInstPrinter> IP(TheTarget->createMCInstPrinter(
      Triple(TripleName), /*AssemblerDialect=*/0, *MAI, *MCII, *MRI));
  if (!IP) {
    WithColor::error()
        << "unable to create instruction printer for target triple '"
        << TheTriple.normalize()  << "\n";
    return 1;
  }

  mca::InstrBuilder IB(*STI, *MCII, *MRI, MCIA.get());

  mca::Context MCA(*MRI, *STI);
  MCA.createMetadataRegistry();

  mca::PipelineOptions PO(/*MicroOpQueue=*/0, /*DecoderThroughput=*/0,
                          /*DispatchWidth=*/0,
                          /*RegisterFileSize=*/0,
                          /*LoadQueueSize=*/0, /*StoreQueueSize=*/0,
                          /*AssumeNoAlias=*/true,
                          /*EnableBottleneckAnalysis=*/false);

  std::error_code EC;
  ToolOutputFile OF(OutputFilename, EC, sys::fs::OF_Text);
  if (EC) {
    WithColor::error() << EC.message() << "\n";
    return 1;
  }

  mcad::MCAWorker Worker(*TheTarget, *STI,
                         MCA, PO, IB, OF,
                         *Ctx, *MAI, *MCII, *IP);

  if(int Ret = initializeProfilers())
    return Ret;

  if (!BrokerPluginPath.empty())
    UseBroker = BT_Plugin;

  // Select the broker
  auto BF = Worker.getBrokerFacade();
  switch (UseBroker) {
  case BT_AsmFile:
    LLVM_DEBUG(dbgs() << "Using AsmFile broker\n");
    mcad::AsmFileBroker::Register(BF);
    break;
  case BT_RawBytes:
    llvm_unreachable("Not implemented yet");
    break;
  case BT_Plugin: {
    auto PluginOrErr = BrokerPlugin::Load(BrokerPluginPath);
    if (!PluginOrErr) {
      handleAllErrors(PluginOrErr.takeError(),
                      [](const ErrorInfoBase &E) {
                        E.log(WithColor::error());
                        errs() << "\n";
                      });
      return 1;
    }
    BrokerPlugin &BP = *PluginOrErr;
    LLVM_DEBUG(dbgs() << "Using broker plugin "
                      << BP.getPluginName() << ", version "
                      << BP.getPluginVersion() << "\n");

    auto strToCstr = [](const std::string& Str) { return Str.c_str(); };
    using iterator = mapped_iterator<typename cl::list<std::string>::iterator,
                                     decltype(strToCstr)>;
    std::vector<const char*> Args(iterator(BrokerPluginArg.begin(), strToCstr),
                                  iterator(BrokerPluginArg.end(), strToCstr));

    // Relay this option to Broker
    if (EnableTimer)
      Args.push_back("-enable-timer");

    BP.registerBroker(Args, BF);
    break;
  }
  }

  if(auto E = Worker.run()) {
    // TODO: Better error message
    handleAllErrors(std::move(E),
                    [](const ErrorInfoBase &E) {
                      E.log(WithColor::error());
                      errs() << "\n";
                    });
    return 1;
  }

  OF.keep();

  if (!EnableTimer)
    llvm::TimerGroup::clearAll();

#ifdef LLVM_MCAD_ENABLE_PROFILER
  if (EnableCpuProfile && ProfilersManager.CpuProfilerStarted)
    ProfilerStop();
#endif

#ifdef LLVM_MCAD_ENABLE_TCMALLOC
  if (EnableHeapProfile && ProfilersManager.HeapProfilerStarted)
    HeapProfilerStop();
#endif

  return 0;
}
