#ifndef LLVM_MCAD_BROKER_BROKERPLUGIN_H
#define LLVM_MCAD_BROKER_BROKERPLUGIN_H
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>

namespace llvm {
namespace mcad {
// Forward declaration
class BrokerFacade;

#define LLVM_MCAD_BROKER_PLUGIN_API_VERSION 1

extern "C" {
struct BrokerPluginLibraryInfo {
  uint32_t APIVersion;

  const char *PluginName;

  const char *PluginVersion;

  void (*BrokerRegistrationCallback)(int argc, const char *const *argv,
                                     BrokerFacade &);
};
}

struct BrokerPlugin {
  static Expected<BrokerPlugin> Load(const std::string &Filename);

  StringRef getFilename() const { return Filename; }

  StringRef getPluginName() const { return Info.PluginName; }

  StringRef getPluginVersion() const { return Info.PluginVersion; }

  uint32_t getAPIVersion() const { return Info.APIVersion; }

  void registerBroker(ArrayRef<const char*> Args, BrokerFacade &BF) const {
    Info.BrokerRegistrationCallback(Args.size(), Args.data(), BF);
  }

private:
  BrokerPlugin(const std::string &Filename, const sys::DynamicLibrary &Library)
      : Filename(Filename), Library(Library), Info() {}

  std::string Filename;
  sys::DynamicLibrary Library;
  BrokerPluginLibraryInfo Info;
};
} // end namespace mcad
} // end namespace llvm

extern "C" ::llvm::mcad::BrokerPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
mcadGetBrokerPluginInfo();
#endif
