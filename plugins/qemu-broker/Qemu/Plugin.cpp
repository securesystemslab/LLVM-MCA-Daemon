#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
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
#include <unistd.h>

using namespace llvm;

#define DEBUG_TYPE "mcad-qemu-relay"

static cl::opt<std::string>
  ConnectAddr("addr", cl::desc("Remote address to connect"),
              cl::Required);

static cl::opt<unsigned>
  ConnectPort("port", cl::desc("Remote port to connect"),
              cl::Required);

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

/// === QEMU Callbacks ===

static void tbExecCallback(unsigned int CPUId, void *Data) {
  using namespace mcad;

  auto TBIdx = (size_t)Data;

  uint64_t VAddr = TBIdx;
  if (CurrentQemuTarget.startswith_lower("arm") &&
      RegInfoRegistry.count("cpsr") && RegInfoRegistry.count("pc")) {
    uint32_t CPSR, PC;
    const auto &PCRegInfo = RegInfoRegistry["pc"],
               &CPSRRegInfo = RegInfoRegistry["cpsr"];
    qemu_plugin_vcpu_read_register(PCRegInfo.RegId, &PC, PCRegInfo.Size);
    qemu_plugin_vcpu_read_register(CPSRRegInfo.RegId, &CPSR, CPSRRegInfo.Size);

    if (CPSR & 0b100000)
      // Thumb mode
      PC |= 0b1;

    VAddr = PC;
  }

  flatbuffers::FlatBufferBuilder Builder(128);
  auto FbExecTB = fbs::CreateExecTB(Builder, TBIdx, VAddr);
  auto FbMessage = fbs::CreateMessage(Builder, fbs::Msg_ExecTB,
                                      FbExecTB.Union());
  fbs::FinishSizePrefixedMessageBuffer(Builder, FbMessage);

  int NumBytesSent = write(RemoteSockt,
                           Builder.GetBufferPointer(), Builder.GetSize());
  if (NumBytesSent < 0) {
    ::perror("Failed to send TB exec");
  }
}

static void tbTranslateCallback(qemu_plugin_id_t Id,
                                struct qemu_plugin_tb *TB) {
  using namespace mcad;

  size_t NumInsn = qemu_plugin_tb_n_insns(TB);
  std::vector<uint8_t> RawInst;
  SmallVector<decltype(RawInst), 4> RawInsts;
  for (auto i = 0U; i < NumInsn; ++i) {
    const auto *QI = qemu_plugin_tb_get_insn(TB, i);
    size_t InsnSize = qemu_plugin_insn_size(QI);
    const auto *I = (const uint8_t*)qemu_plugin_insn_data(QI);

    RawInst.clear();
    for (auto j = 0U; j < InsnSize; ++j)
      RawInst.push_back(*(I++));

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
}

static void onPluginExit(qemu_plugin_id_t Id, void *Data) {
  using namespace mcad;

  if (RemoteSockt < 0)
    return;

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

  // Connect to remote MCAD
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

  NumTranslationBlock = 0U;

  qemu_plugin_register_vcpu_tb_trans_cb(Id, tbTranslateCallback);
  qemu_plugin_register_atexit_cb(Id, onPluginExit, /*Data=*/nullptr);

  return 0;
}
