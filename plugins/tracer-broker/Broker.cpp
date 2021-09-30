#include "BrokerFacade.h"
#include "Brokers/Broker.h"
#include "Brokers/BrokerPlugin.h"

using namespace llvm;
using namespace mcad;

#define DEBUG_TYPE "mcad-tracer-broker"

namespace {
class TracerBroker : public Broker {
  unsigned getFeatures() const override {
    return Broker::Feature_Metadata;
  }
};
} // end anonymous namespace

extern "C" ::llvm::mcad::BrokerPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
mcadGetBrokerPluginInfo() {
  return {
    LLVM_MCAD_BROKER_PLUGIN_API_VERSION, "TracerBroker", "v0.1",
    [](int argc, const char *const *argv, BrokerFacade &BF) {
      BF.setBroker(std::make_unique<TracerBroker>());
    }
  };
}
