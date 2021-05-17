#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/WithColor.h"
#include <string>

#include <arpa/inet.h>
#include <cstdlib>
#include <cstdio>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Serialization/mcad_generated.h"

using namespace llvm;
using namespace mcad;

#define DEBUG_TYPE "qemu-broker-client"

static cl::opt<std::string>
  ConnectAddr("addr", cl::Required);

static cl::opt<unsigned>
  ConnectPort("port", cl::Required);

static cl::opt<std::string>
  InputFilename(cl::Positional, cl::value_desc("filename"),
                cl::init("-"));

enum ActionKind {
  AK_SendTB,
  AK_TBExec
};

static cl::opt<ActionKind>
  Action("action", cl::desc("Action to perform"),
         cl::values(
           clEnumValN(AK_SendTB, "send-tb", "Send a TranslationBlock"),
           clEnumValN(AK_TBExec, "tb-exec", "Signal that a TB is executed")
         ),
         cl::init(AK_SendTB));

static cl::opt<unsigned>
  TBSentIdx("tb-sent-index", cl::desc("The index of the sent TB"),
            cl::init(0U));

static cl::opt<unsigned>
  TBExecIdx("tb-exec", cl::desc("The index of the executed TB"),
            cl::init(0U));

static cl::opt<unsigned>
  TBExecAddr("tb-exec-addr", cl::desc("The VAddr which TB is executed"),
             cl::init(0U));

static void sendTB(int Sockt) {
  auto ErrOrBuffer = MemoryBuffer::getFileOrSTDIN(InputFilename, /*IsText=*/true);
  if (!ErrOrBuffer) {
    WithColor::error() << "Failed to open the file: "
                       << ErrOrBuffer.getError().message() << "\n";
    return;
  }
  StringRef Buffer = (*ErrOrBuffer)->getBuffer();

  SmallVector<StringRef, 4> Lines;
  SmallVector<StringRef, 4> RawBytes;
  std::vector<uint8_t> Bytes;
  SmallVector<decltype(Bytes), 4> Insts;
  Buffer.split(Lines, '\n', -1, false);
  for (const auto &Line : Lines) {
    if (Line.startswith("//") || Line.startswith("#"))
      // Treat as comment
      continue;
    RawBytes.clear();
    Line.trim().split(RawBytes, ',', -1, false);
    Bytes.clear();
    for (const StringRef &RawByte : RawBytes) {
      uint8_t Byte;
      if (!RawByte.trim().getAsInteger(0, Byte))
        Bytes.push_back(Byte);
    }
    Insts.push_back(Bytes);
  }
  if (Insts.empty())
    return;

  // Build the flatbuffers
  flatbuffers::FlatBufferBuilder Builder(1024);
  std::vector<flatbuffers::Offset<fbs::Inst>> FbInsts;
  for (const auto &Inst : Insts) {
    auto InstData = Builder.CreateVector(Inst);
    auto FbInst = fbs::CreateInst(Builder, InstData);
    FbInsts.push_back(FbInst);
  }
  auto FbInstructions = Builder.CreateVector(FbInsts);
  auto FbTB = fbs::CreateTranslatedBlock(Builder, TBSentIdx, FbInstructions);
  auto FbMessage = fbs::CreateMessage(Builder, fbs::Msg_TranslatedBlock,
                                      FbTB.Union());
  fbs::FinishSizePrefixedMessageBuffer(Builder, FbMessage);

  errs() << "Sending TB with "
         << FbInsts.size() << " instructions "
         << ". Size = " << Builder.GetSize() << " bytes\n";
  int NumBytesSent = write(Sockt, Builder.GetBufferPointer(), Builder.GetSize());
  if (NumBytesSent < 0) {
    ::perror("Failed to send TB data");
  }
  LLVM_DEBUG(dbgs() << "Sent " << NumBytesSent << " bytes\n");
}

static void tbExec(int Sockt) {
  flatbuffers::FlatBufferBuilder Builder(128);
  auto FbExecTB = fbs::CreateExecTB(Builder, TBExecIdx, TBExecAddr);
  auto FbMessage = fbs::CreateMessage(Builder, fbs::Msg_ExecTB,
                                      FbExecTB.Union());
  fbs::FinishSizePrefixedMessageBuffer(Builder, FbMessage);

  errs() << "Sending execution signal of TB " << TBExecIdx
         << " with address " << format_hex(TBExecAddr, 16) << "\n";
  int NumBytesSent = write(Sockt, Builder.GetBufferPointer(), Builder.GetSize());
  if (NumBytesSent < 0) {
    ::perror("Failed to send TB data");
  }
  LLVM_DEBUG(dbgs() << "Sent " << NumBytesSent << " bytes\n");
}

namespace {
// A simple RAII to cleanup socket resources
struct SocketRAII {
  int SocktFD;
  explicit SocketRAII(int FD) : SocktFD(FD) {}
  ~SocketRAII() {
    LLVM_DEBUG(dbgs() << "Cleaning up socket...\n");
    ::shutdown(SocktFD, SHUT_RDWR);
    ::close(SocktFD);
  }
};
} // end anonymous namespace

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv, "");

  int Sockt = socket(AF_INET , SOCK_STREAM , 0);
  if (Sockt < 0) {
    errs() << "Failed to create socket\n";
    return 1;
  }

  sockaddr_in ServAddr;
  ServAddr.sin_addr.s_addr = inet_addr(ConnectAddr.c_str());
  ServAddr.sin_family = AF_INET;
  ServAddr.sin_port = htons(ConnectPort);
  if (connect(Sockt, (sockaddr*)&ServAddr, sizeof(ServAddr)) < 0) {
    errs() << "Failed to connect to "
           << ConnectAddr << ":" << ConnectPort << "\n";
    return 1;
  }
  errs() << "Connected to " << ConnectAddr << ":" << ConnectPort << "\n";
  SocketRAII SocktRAII(Sockt);

  switch (Action) {
  case AK_SendTB:
    sendTB(Sockt);
    break;
  case AK_TBExec:
    tbExec(Sockt);
    break;
  }

  return 0;
}
