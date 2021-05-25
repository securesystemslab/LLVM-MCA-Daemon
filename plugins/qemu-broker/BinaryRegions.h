#ifndef LLVM_MCAD_QEMU_BROKER_BINARYREGIONS_H
#define LLVM_MCAD_QEMU_BROKER_BINARYREGIONS_H
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <unordered_map>

namespace llvm {
// Forward declarations
class MemoryBuffer;
class raw_ostream;
namespace json {
class Array;
}

namespace mcad {
namespace qemu_broker {
struct BinaryRegion {
  std::string Description;
  uint64_t StartAddr, EndAddr;
};

inline
raw_ostream &operator<<(raw_ostream &OS, const BinaryRegion &BR) {
  OS << "<" << BR.Description << ">, "
     << "Address: [ " << format_hex(BR.StartAddr, 16)
     << " - " << format_hex(BR.EndAddr, 16) << " ]";
  return OS;
}

class BinaryRegions {
  struct BRSymbol {
    uint64_t StartAddr;
    size_t Size;
  };

  Error readSymbols(const MemoryBuffer &RawObjFile,
                    StringMap<BRSymbol> &Symbols);

  Error parseRegions(json::Array &RawRegions,
                     const StringMap<BRSymbol> &Symbols);

  // {start address -> BinaryRegion}
  std::unordered_map<uint64_t, BinaryRegion> Regions;

  BinaryRegions() = default;

public:
  static
  Expected<std::unique_ptr<BinaryRegions>> Create(StringRef ManifestPath);

  size_t size() const { return Regions.size(); }

  const BinaryRegion *lookup(uint64_t StartAddr) const {
    if (!Regions.count(StartAddr))
      return nullptr;
    else
      return &Regions.at(StartAddr);
  }
};
} // end namespace qemu_broker
} // end namespace mcad
} // end namespace llvm
#endif
