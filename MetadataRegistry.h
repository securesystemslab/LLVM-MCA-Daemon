#ifndef LLVM_MCA_METADATAREGISTRY_H
#define LLVM_MCA_METADATAREGISTRY_H
#include "llvm/ADT/Any.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Option/Option.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>

namespace llvm {
namespace mcad {

class MetadataCategory {
  // The reason we don't use IndexedMap here
  // is because we want an easy way to recycle the space
  // in case the number of instructions gets too big.
  // However, it is a little difficult to do that with IndexedMap.
  DenseMap<unsigned, Any> InstEntries;

public:
  inline bool count(unsigned Idx) const {
    return InstEntries.count(Idx);
  }

  template<class T>
  const T *getPtr(unsigned Idx) const {
    auto It = InstEntries.find(Idx);
    if (It != InstEntries.end()) {
      const Any *Res = &It->second;
      return any_cast<T>(Res);
    }
    return nullptr;
  }

  template<class T>
  std::optional<T> get(unsigned Idx) const {
    auto It = InstEntries.find(Idx);
    if (It != InstEntries.end()) {
      const Any &Res = It->second;
      return any_cast<T>(Res);
    }
    return std::nullopt;
  }

  Any &operator[](unsigned Idx) {
    return InstEntries[Idx];
  }

  inline bool erase(unsigned Idx) {
    return InstEntries.erase(Idx);
  }
};

/// A registry that holds metadata for each CustomInstruction.
/// Each entry is indexed by MDToken within CustomInstruction, and
/// recycled upon being released.
class MetadataRegistry {
  SmallVector<std::unique_ptr<MetadataCategory>, 2> Categories;

public:
  MetadataCategory &operator[](unsigned Kind) {
    if (Kind >= Categories.size()) {
      for (int i = 0, S = Kind - Categories.size() + 1; i < S; ++i)
        Categories.emplace_back(nullptr);
    }
    if (!Categories[Kind])
      Categories[Kind] = std::make_unique<MetadataCategory>();
    return *Categories[Kind];
  }

  bool erase(unsigned Idx) {
    bool Success = false;
    // Iterate through all categories
    for (unsigned i = 0U, S = Categories.size(); i < S; ++i) {
      if (auto &Cat = Categories[i])
        Success |= Cat->erase(Idx);
    }
    return Success;
  }
};

} // end namespace mcad
} // end namespace llvm
#endif
