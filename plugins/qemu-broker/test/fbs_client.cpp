#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

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

#define DEBUG_TYPE "llvm-mcad"

static cl::opt<std::string>
  ConnectAddr("addr", cl::Required);

static cl::opt<unsigned>
  ConnectPort("port", cl::Required);

static cl::opt<int>
  NumInstGenerated("num-insts", cl::init(100));

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

  flatbuffers::FlatBufferBuilder Builder(1024);
  std::vector<flatbuffers::Offset<fbs::Inst>> Insts;
  uint8_t DummyInst[] = {0x94, 0x87, 0x94, 0x87};
  for (int i = 0; i < NumInstGenerated; ++i) {
    auto InstData = Builder.CreateVector(DummyInst, 4);
    auto Inst = fbs::CreateInst(Builder, InstData);
    Insts.push_back(Inst);
  }
  auto Instructions = Builder.CreateVector(Insts);
  auto TB = fbs::CreateTranslatedBlock(Builder, 0, Instructions);
  auto Message = fbs::CreateMessage(Builder, fbs::Msg_TranslatedBlock,
                                    TB.Union());

  fbs::FinishSizePrefixedMessageBuffer(Builder, Message);

  errs() << "Sending buffer of size " << Builder.GetSize() << "\n";

  if (write(Sockt, Builder.GetBufferPointer(), Builder.GetSize()) < 0) {
    errs() << "Failed to write data\n";
  }

  close(Sockt);

  return 0;
}
