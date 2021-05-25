#ifndef LLVM_MCAD_QEMU_BROKER_BINARYREGIONS_H
#define LLVM_MCAD_QEMU_BROKER_BINARYREGIONS_H
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

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
  StringRef Description;
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

  // Owner of binary file
  // We need to keep object::Binary instance "alive"
  // because both BRSymbol and BinaryRegion contain StringRef
  // from it.
  std::unique_ptr<object::Binary> TheBinary;

  SmallVector<BinaryRegion, 2> Regions;

  BinaryRegions() = default;

public:
  static
  Expected<std::unique_ptr<BinaryRegions>> Create(StringRef ManifestPath);

  using iterator = typename decltype(Regions)::iterator;
  using const_iterator = typename decltype(Regions)::const_iterator;

  iterator begin() { return Regions.begin(); }
  iterator end() { return Regions.end(); }
  const_iterator begin() const { return Regions.begin(); }
  const_iterator end() const { return Regions.end(); }

  size_t size() const { return Regions.size(); }
};
} // end namespace qemu_broker
} // end namespace mcad
} // end namespace llvm
#endif
