#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/WithColor.h"
#include <string>
#include <unordered_map>
#include <vector>

#include "Serialization/mcad_generated.h"
extern "C" {
#include "qemu/qemu-plugin.h"
}

#include <arpa/inet.h>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace llvm;

#define DEBUG_TYPE "mcad-qemu-relay"

static cl::opt<std::string>
  ConnectAddr("addr", cl::desc("Remote address or Unix socket path to connect"),
              cl::Required);

static cl::opt<unsigned>
  ConnectPort("port", cl::desc("Remote port to connect or 0 to use Unix socket"),
              cl::init(0U));

static cl::opt<bool>
  OnlyMainCode("only-main-code", cl::desc("Only send instructions that "
                                          "are part of the main executable"),
               cl::init(false), cl::Hidden);

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static StringRef CurrentQemuTarget;

namespace {
struct RegisterInfo {
  int RegId;
  // Number of bytes
  uint8_t Size;
};
} // end anonymous namespace
static
std::unordered_map<const char*, RegisterInfo> RegInfoRegistry;

static int RemoteSockt = -1;

static size_t NumTranslationBlock = 0U;
#ifndef NDEBUG
static SmallVector<size_t, 8> TBNumInsts;
static size_t NumExecInsts = 0U;
#endif

/// === QEMU Callbacks ===

namespace {
struct ExecTransBlock {
  struct MemAccess {
    uint32_t InstIdx = 0U;
    uint64_t VAddr = 0U;
    uint8_t  Size = 0U;
    bool IsStore = false;
  };

  uint32_t TBIdx = 0U;
  uint64_t PC = 0U;
  SmallVector<MemAccess, 2> MemAccesses;

  void reset() {
    MemAccesses.clear();
    TBIdx = 0U;
    PC = 0U;
  }
};
} // end anonymous namespace

static llvm::Optional<ExecTransBlock> CurrentExecTB;

static inline void flushPreviousTBExec() {
  using namespace mcad;
  if (!CurrentExecTB)
    return;

  flatbuffers::FlatBufferBuilder Builder(128);
  flatbuffers::Offset<flatbuffers::Vector<const fbs::MemoryAccess*>> MAV;
  if (CurrentExecTB->MemAccesses.size()) {
    std::vector<fbs::MemoryAccess> FbMemAccesses;
    for (const auto &MA : CurrentExecTB->MemAccesses)
      FbMemAccesses.emplace_back(MA.InstIdx, MA.VAddr, MA.Size, MA.IsStore);

    MAV = Builder.CreateVectorOfStructs(FbMemAccesses);
  }

  fbs::ExecTBBuilder ETB(Builder);
  ETB.add_Index(CurrentExecTB->TBIdx);
  ETB.add_PC(CurrentExecTB->PC);
  if (CurrentExecTB->MemAccesses.size())
    ETB.add_MemAccesses(MAV);
  auto FbExecTB = ETB.Finish();
  auto FbMessage = fbs::CreateMessage(Builder, fbs::Msg_ExecTB,
                                      FbExecTB.Union());
  fbs::FinishSizePrefixedMessageBuffer(Builder, FbMessage);

  int NumBytesSent = write(RemoteSockt,
                           Builder.GetBufferPointer(), Builder.GetSize());
  if (NumBytesSent < 0) {
    ::perror("Failed to send TB exec");
  }

  // Reset the current ExecTB
  CurrentExecTB->reset();
}

static void tbExecCallback(unsigned int CPUId, void *Data) {
  using namespace mcad;

  flushPreviousTBExec();
  if (!CurrentExecTB)
    CurrentExecTB = ExecTransBlock();

  auto TBIdx = (size_t)Data;
#ifndef NDEBUG
  if (TBIdx < TBNumInsts.size())
    NumExecInsts += TBNumInsts[TBIdx];
#endif

  uint64_t VAddr = TBIdx;
  if (CurrentQemuTarget.startswith_lower("arm") &&
      RegInfoRegistry.count("cpsr") && RegInfoRegistry.count("pc")) {
    uint32_t CPSR, PC;
    const auto &PCRegInfo = RegInfoRegistry["pc"],
               &CPSRRegInfo = RegInfoRegistry["cpsr"];
    if (qemu_plugin_vcpu_read_register(PCRegInfo.RegId, &PC,
                                       PCRegInfo.Size) > 0 &&
        qemu_plugin_vcpu_read_register(CPSRRegInfo.RegId, &CPSR,
                                       CPSRRegInfo.Size) > 0) {
      if (CPSR & 0b100000)
        // Thumb mode
        PC |= 0b1;

      VAddr = PC;
    } else
      WithColor::error() << "Failed to read ARM PC or CPSR register\n";
  }
  if (CurrentQemuTarget.startswith_lower("x86_64") &&
      RegInfoRegistry.count("rip")) {
    const auto &RIPRegInfo = RegInfoRegistry["rip"];
    if (qemu_plugin_vcpu_read_register(RIPRegInfo.RegId, &VAddr,
                                       RIPRegInfo.Size) <= 0)
      WithColor::error() << "Failed to read X86_64 RIP register\n";
  }

  CurrentExecTB->TBIdx = TBIdx;
  CurrentExecTB->PC = VAddr;
}

static void onMemoryOps(unsigned int CPUIdx, qemu_plugin_meminfo_t MemInfo,
                        uint64_t VAddr, void *UData) {
  assert(CurrentExecTB);
  auto InstIdx = (uintptr_t)UData;
  auto &MemAccesses = CurrentExecTB->MemAccesses;

  unsigned int ShiftedSize = qemu_plugin_mem_size_shift(MemInfo);
  bool IsStore = qemu_plugin_mem_is_store(MemInfo);
  MemAccesses.push_back({static_cast<uint32_t>(InstIdx),
                         VAddr,
                         static_cast<uint8_t>(2 << ShiftedSize), IsStore});
}

// The starting address of the currently loaded binary
// TODO: What about end address?
static llvm::Optional<uint64_t> CodeStartAddr;

static void sendCodeStartAddr() {
  using namespace mcad;
  assert(CodeStartAddr.hasValue());

  flatbuffers::FlatBufferBuilder Builder(16);
  auto FbMD = fbs::CreateMetadata(Builder, *CodeStartAddr);
  auto FbMessage = fbs::CreateMessage(Builder, fbs::Msg_Metadata,
                                      FbMD.Union());
  fbs::FinishSizePrefixedMessageBuffer(Builder, FbMessage);

  int NumBytesSent = write(RemoteSockt,
                           Builder.GetBufferPointer(), Builder.GetSize());
  if (NumBytesSent < 0) {
    ::perror("Failed to send TB data");
  }
}

static void tbTranslateCallback(qemu_plugin_id_t Id,
                                struct qemu_plugin_tb *TB) {
  using namespace mcad;

  if (!CodeStartAddr) {
    CodeStartAddr = qemu_plugin_vcpu_code_start_vaddr();
    LLVM_DEBUG(dbgs() << "Code start address: "
                      << format_hex(*CodeStartAddr, 16) << "\n");
    sendCodeStartAddr();
  }

  size_t NumInsn = qemu_plugin_tb_n_insns(TB);
  std::vector<uint8_t> RawInst;
  SmallVector<decltype(RawInst), 4> RawInsts;
  for (auto i = 0U; i < NumInsn; ++i) {
    const auto *QI = qemu_plugin_tb_get_insn(TB, i);
    size_t InsnSize = qemu_plugin_insn_size(QI);
    const auto *I = (const uint8_t*)qemu_plugin_insn_data(QI);
    uint64_t VAddr = qemu_plugin_insn_vaddr(QI);

    // Filter by address
    if (OnlyMainCode && VAddr < *CodeStartAddr)
      // Ignore code that is not part of the main binary
      // (e.g. interpreter)
      continue;

    RawInst.clear();
    for (auto j = 0U; j < InsnSize; ++j)
      RawInst.push_back(*(I++));

    // instrumenting memory ops
    qemu_plugin_register_vcpu_mem_cb((struct qemu_plugin_insn*)QI, onMemoryOps,
                                     QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_MEM_RW,
                                     (void*)static_cast<uintptr_t>(i));

    RawInsts.emplace_back(std::move(RawInst));
  }
  if (RawInsts.empty()) return;

  // Send the raw instructions
  flatbuffers::FlatBufferBuilder Builder(1024);
  std::vector<flatbuffers::Offset<fbs::Inst>> FbInsts;
  for (const auto &Inst : RawInsts) {
    auto InstData = Builder.CreateVector(Inst);
    auto FbInst = fbs::CreateInst(Builder, InstData);
    FbInsts.push_back(FbInst);
  }
  auto FbInstructions = Builder.CreateVector(FbInsts);
  auto FbTB = fbs::CreateTranslatedBlock(Builder,
                                         NumTranslationBlock, FbInstructions);
  auto FbMessage = fbs::CreateMessage(Builder, fbs::Msg_TranslatedBlock,
                                      FbTB.Union());
  fbs::FinishSizePrefixedMessageBuffer(Builder, FbMessage);

  int NumBytesSent = write(RemoteSockt,
                           Builder.GetBufferPointer(), Builder.GetSize());
  if (NumBytesSent < 0) {
    ::perror("Failed to send TB data");
    return;
  }

  // Register exec callback
#ifndef NDEBUG
  TBNumInsts.push_back(RawInsts.size());
#endif
  qemu_plugin_register_vcpu_tb_exec_cb(TB, tbExecCallback,
                                       QEMU_PLUGIN_CB_NO_REGS,
                                       (void*)NumTranslationBlock++);

  // Since qemu_plugin_vcpu_get_register_info can only be invoked
  // after the CPU is running, we can't call it in vcpu init callback.
  if (CurrentQemuTarget.startswith_lower("arm")) {
    if (!RegInfoRegistry.count("cpsr")) {
      // TODO: Cortex-M series processor, which uses
      // XPSR instead of CPSR
      uint8_t RegSize;
      int RegId = qemu_plugin_vcpu_get_register_info("cpsr",
                                                     &RegSize);
      if (RegId < 0) {
        errs() << "Failed to get register id of ARM cpsr from QEMU\n";
      } else {
        RegInfoRegistry.insert({"cpsr", {RegId, RegSize}});
      }
    }
    if (!RegInfoRegistry.count("pc")) {
      uint8_t RegSize;
      int RegId = qemu_plugin_vcpu_get_register_info("pc",
                                                     &RegSize);
      if (RegId < 0) {
        errs() << "Failed to get register id of ARM pc from QEMU\n";
      } else {
        RegInfoRegistry.insert({"pc", {RegId, RegSize}});
      }
    }
  }

  if (CurrentQemuTarget.startswith_lower("x86_64")) {
    if (!RegInfoRegistry.count("rip")) {
      uint8_t RegSize;
      int RegId = qemu_plugin_vcpu_get_register_info("rip",
                                                     &RegSize);
      if (RegId < 0) {
        errs() << "Failed to get register id of X86_64 RIP from QEMU\n";
      } else {
        RegInfoRegistry.insert({"rip", {RegId, RegSize}});
      }
    }
  }
}

static void onPluginExit(qemu_plugin_id_t Id, void *Data) {
  using namespace mcad;

  LLVM_DEBUG(dbgs() << "Total number of executed instructions: "
                    << NumExecInsts << "\n");

  if (RemoteSockt < 0)
    return;

  flushPreviousTBExec();

  // Use TBIdx == 0xffffffff as end signal
  flatbuffers::FlatBufferBuilder Builder(128);
  auto FbExecTB = fbs::CreateExecTB(Builder, ~uint32_t(0U), ~uint64_t(0U));
  auto FbMessage = fbs::CreateMessage(Builder, fbs::Msg_ExecTB,
                                      FbExecTB.Union());
  fbs::FinishSizePrefixedMessageBuffer(Builder, FbMessage);

  int NumBytesSent = write(RemoteSockt,
                           Builder.GetBufferPointer(), Builder.GetSize());
  if (NumBytesSent < 0) {
    ::perror("Failed to send end signal");
  }

  ::shutdown(RemoteSockt, SHUT_RDWR);
  ::close(RemoteSockt);
}

static int connectInetSocket() {
  RemoteSockt = socket(AF_INET , SOCK_STREAM , 0);
  if (RemoteSockt < 0) {
    WithColor::error() << "Failed to create socket\n";
    return 1;
  }

  sockaddr_in ServAddr;
  ServAddr.sin_addr.s_addr = inet_addr(ConnectAddr.c_str());
  ServAddr.sin_family = AF_INET;
  ServAddr.sin_port = htons(ConnectPort);
  if (connect(RemoteSockt, (sockaddr*)&ServAddr, sizeof(ServAddr)) < 0) {
    WithColor::error() << "Failed to connect to "
                       << ConnectAddr << ":" << ConnectPort << "\n";
    return 1;
  }
  WithColor::note() << "Connected to "
                    << ConnectAddr << ":" << ConnectPort << "\n";
  return 0;
}

static int connectUnixSocket() {
  RemoteSockt = socket(AF_UNIX, SOCK_STREAM, 0);
  if (RemoteSockt < 0) {
    WithColor::error() << "Failed to create socket\n";
    return 1;
  }

  sockaddr_un ServAddr;
  ServAddr.sun_family = AF_UNIX;
  strcpy(ServAddr.sun_path, ConnectAddr.c_str());
  if (connect(RemoteSockt, (sockaddr *)&ServAddr, sizeof(ServAddr)) < 0) {
    WithColor::error() << "Failed to connect to " << ConnectAddr << "\n";
    return 1;
  }
  WithColor::note() << "Connected to " << ConnectAddr << "\n";
  return 0;
}

extern "C" QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t Id, const qemu_info_t *Info,
                        int argc, char **argv) {
  // Insert a fake argv0 at the beginning
  SmallVector<const char*, 4> Argv;
  Argv.push_back("MCADRelay");
  Argv.append(argv, argv + argc);
  Argv.push_back(nullptr);

  cl::ParseCommandLineOptions(argc + 1, Argv.data());

  CurrentQemuTarget = Info->target_name;
  LLVM_DEBUG(dbgs() << "Using QEMU target " << CurrentQemuTarget << "\n");

  // Connect to MCAD
  if (ConnectPort) {
    if (int Ret = connectInetSocket())
      return Ret;
  } else {
    if (int Ret = connectUnixSocket())
      return Ret;
  }

  NumTranslationBlock = 0U;

  qemu_plugin_register_vcpu_tb_trans_cb(Id, tbTranslateCallback);
  qemu_plugin_register_atexit_cb(Id, onPluginExit, /*Data=*/nullptr);

  return 0;
}
