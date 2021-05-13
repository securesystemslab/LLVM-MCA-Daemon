#include "llvm/Support/raw_ostream.h"

#include "Brokers/BrokerPlugin.h"

using namespace llvm;
using namespace mcad;

static void printArg(int argc, const char *const *argv) {
  errs() << "Got some arguments:\n";
  for (int i = 0; i < argc; ++i) {
    errs() << argv[i] << "\n";
  }
}

extern "C" ::llvm::mcad::BrokerPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
mcadGetBrokerPluginInfo() {
  return {LLVM_MCAD_BROKER_PLUGIN_API_VERSION,
          "QemuBroker", "v0.1",
          [](int argc, const char *const *argv, BrokerFacade &BF) {
            printArg(argc, argv);
          }};
}
