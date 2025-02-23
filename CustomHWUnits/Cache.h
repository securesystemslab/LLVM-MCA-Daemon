#ifndef LLVM_MCAD_CACHE_H
#define LLVM_MCAD_CACHE_H

#include "CustomHWUnits/AbstractBranchPredictorUnit.h"
#include "MetadataCategories.h"
#include "Statistics.h"

#include <map>
#include <memory>

namespace llvm {
namespace mcad {

class CacheUnit {
private:
  /// Size of the cache in number of bytes.
  unsigned size = 2048;
  /// Number of bytes in a cache line.
  unsigned lineSize = 64;
  /// Number of lines in the cache
  unsigned numLines;
  /// Number of ways in the cache.
  unsigned numWays = 2;
  /// Number of the sets in the cache.
  unsigned numSets;
  /// Latency to access the cache.
  unsigned latency = 2;

  std::vector<std::vector<MDInstrAddr>> table;
  std::shared_ptr<CacheUnit> nextLevelCache;

protected:
    // A protected constructor to allow for the creation of a memory object.
    CacheUnit() = default;

public:

  struct Statistics {
    OverflowableCount numLoadMisses = {};
    OverflowableCount numLoadCycles = {};
    OverflowableCount numStoreCycles = {};
  };

  Statistics stats = {};

  CacheUnit(unsigned size, unsigned numWays, std::shared_ptr<CacheUnit> nextLevelCache, unsigned latency)
      : size(size), numWays(numWays), numLines(size / lineSize),
        numSets(numLines / numWays),
        table(numSets, std::vector<MDInstrAddr>(numWays)),
        nextLevelCache(nextLevelCache),
        latency(latency) {
            assert(size % lineSize == 0);
            assert(nextLevelCache != nullptr);
            assert(stats.numLoadMisses.count == 0);
        };

  /// Loads the cacheline from the cache.
  /// If the cacheline is not in the cache, we will evict an entry and load the cacheline from the next level.
  virtual unsigned load(MDInstrAddr IA) {
    unsigned rtn = latency;
    MDInstrAddr* entry = getEntry(IA);
    if (entry == nullptr) {
      entry = evictEntry(IA);
      *entry = getCachelineAddress(IA);
      rtn += nextLevelCache->load(IA);
      stats.numLoadMisses.inc(1);
    }
    stats.numLoadCycles.inc(rtn);
    return rtn;
  }

  /// Write the address to the cache and write-through to the next level.
  /// If there is no existing entry, we will evict an entry and replace it with the new cacheline.
  virtual unsigned store(MDInstrAddr IA) {
    MDInstrAddr* entry = getEntry(IA);
    if (entry == nullptr) {
      entry = evictEntry(IA);
      *entry = getCachelineAddress(IA);
    } 
    const unsigned rtn = latency + nextLevelCache->store(IA); 
    stats.numStoreCycles.inc(rtn);
    return rtn;
  }

  /// Return the entry if it is in the cache, otherwise return nullptr.
  ///
  /// Since we are only simulating the latency, we can simply return the address of
  /// the cacheline and omit the data.
  virtual MDInstrAddr* getEntry(MDInstrAddr IA) {
    // Check if the address is in the cache
    unsigned setIndex = getSetIndex(IA);
    for (auto &way : table[setIndex]) {
      if (way.addr == IA.addr) {
        return &way;
      }
    }

    // The address is not in the cache.
    return nullptr;
  }

  /// Find an entry to evict and returns the evicted cacheline.
  ///
  /// For simplicity, we will randomly evict an entry in the set if the set if full.
  virtual MDInstrAddr* evictEntry(MDInstrAddr IA) {
    unsigned setIndex = getSetIndex(IA);
    // Attempt to find an empty entry.
    for (auto &way : table[setIndex]) {
      if (way.addr == 0) {
        return &way;
      }
    }

    // Evict a random entry.
    unsigned wayIndex = rand() % numWays;
    return &table[setIndex][wayIndex];
  }

  /// Return the cacheline-agligned address.
  MDInstrAddr getCachelineAddress(MDInstrAddr IA) {
    MDInstrAddr rtn = IA;
    rtn.addr -= rtn.addr % lineSize;
    return rtn;
  }

  /// Returns the the index of the set that the address maps to.
  unsigned getSetIndex(MDInstrAddr IA) { return getCachelineAddress(IA).addr % numSets; }
};

/// A simple memory object that simulates a memory with a fixed load/store latency.
class MemoryUnit : public CacheUnit {
public:
  MemoryUnit(unsigned latency) : CacheUnit(), latency(latency) {}

  unsigned load(MDInstrAddr IA) override { return latency; }
  unsigned store(MDInstrAddr IA) override { return latency; }

private:
  unsigned latency = 300;
};

} // namespace mcad
} // namespace llvm

#endif /* LLVM_MCAD_CACHE_H */
