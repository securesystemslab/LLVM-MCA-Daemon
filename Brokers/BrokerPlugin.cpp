#include "BrokerPlugin.h"

using namespace llvm;
using namespace mcad;

Expected<BrokerPlugin> BrokerPlugin::Load(const std::string &Filename) {
  std::string Error;
  auto Library =
      sys::DynamicLibrary::getPermanentLibrary(Filename.c_str(), &Error);
  if (!Library.isValid())
    return make_error<StringError>(Twine("Could not load library '") +
                                       Filename + "': " + Error,
                                   inconvertibleErrorCode());

  BrokerPlugin P{Filename, Library};
  intptr_t getDetailsFn =
      (intptr_t)Library.SearchForAddressOfSymbol("mcadGetBrokerPluginInfo");

  if (!getDetailsFn)
    // If the symbol isn't found, this is probably a legacy plugin, which is an
    // error
    return make_error<StringError>(Twine("Plugin entry point not found in '") +
                                       Filename + "'. Is this a legacy plugin?",
                                   inconvertibleErrorCode());

  P.Info = reinterpret_cast<decltype(mcadGetBrokerPluginInfo) *>(getDetailsFn)();

  if (P.Info.APIVersion != LLVM_MCAD_BROKER_PLUGIN_API_VERSION)
    return make_error<StringError>(
        Twine("Wrong API version on plugin '") + Filename + "'. Got version " +
            Twine(P.Info.APIVersion) + ", supported version is " +
            Twine(LLVM_MCAD_BROKER_PLUGIN_API_VERSION) + ".",
        inconvertibleErrorCode());

  if (!P.Info.BrokerRegistrationCallback)
    return make_error<StringError>(Twine("Empty entry callback in plugin '") +
                                       Filename + "'.'",
                                   inconvertibleErrorCode());

  return P;
}
