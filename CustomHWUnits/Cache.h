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

  struct CacheEntry {
    MDInstrAddr cachedAddress;
    unsigned lastAccess;
  };

  std::vector<std::vector<struct CacheEntry>> table;
  std::vector<unsigned> nextFree = {};
  std::shared_ptr<CacheUnit> nextLevelCache;

  // Monotonically increasing by one for every load or store that this 
  // CacheUnit sees. Used for the LRU policy.
  // FIXME: fix wraparound
  unsigned long long localClock = 0;

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
        table(numSets, std::vector<struct CacheEntry>(numWays)),
        nextFree(numSets),
        nextLevelCache(nextLevelCache),
        latency(latency) {
            assert(size % lineSize == 0);
            assert(nextLevelCache != nullptr);
            assert(stats.numLoadMisses.count == 0);
        };

  /// Loads the cacheline from the cache.
  /// If the cacheline is not in the cache, we will evict an entry and load the cacheline from the next level.
  virtual unsigned load(MDInstrAddr IA) {
    localClock++;
    unsigned rtn = latency;
    struct CacheEntry* entry = getEntry(IA);
    if (entry == nullptr) {
      entry = evictEntry(IA);
      entry->cachedAddress = getCachelineAddress(IA);
      rtn += nextLevelCache->load(IA);
      stats.numLoadMisses.inc(1);
    }
    entry->lastAccess = localClock;
    stats.numLoadCycles.inc(rtn);
    return rtn;
  }

  /// Write the address to the cache and write-through to the next level.
  /// If there is no existing entry, we will evict an entry and replace it with the new cacheline.
  virtual unsigned store(MDInstrAddr IA) {
    localClock++;
    struct CacheEntry *entry = getEntry(IA);
    if (entry != nullptr) {
      entry = evictEntry(IA);
      entry->cachedAddress = getCachelineAddress(IA);
    } 
    entry->lastAccess = localClock;
    const unsigned rtn = latency + nextLevelCache->store(IA); 
    stats.numStoreCycles.inc(rtn);
    return rtn;
  }

  /// Return the cacheline-agligned address.
  MDInstrAddr getCachelineAddress(MDInstrAddr IA) {
    MDInstrAddr rtn = IA;
    rtn.addr -= rtn.addr % lineSize;
    return rtn;
  }

  /// Returns the the index of the set that the address maps to.
  unsigned getSetIndex(MDInstrAddr IA) { return getCachelineAddress(IA).addr % numSets; }

private:
  /// Return the entry if it is in the cache, otherwise return nullptr.
  ///
  /// Since we are only simulating the latency, we can simply return the address of
  /// the cacheline and omit the data.
  struct CacheEntry *getEntry(MDInstrAddr IA) {
    // Check if the address is in the cache
    unsigned setIndex = getSetIndex(IA);
    for (auto &way : table[setIndex]) {
      if (way.cachedAddress.addr == IA.addr) {
        return &way;
      }
    }

    // The address is not in the cache.
    return nullptr;
  }

  /// Find an entry to evict and returns the evicted cacheline.
  ///
  /// For simplicity, we will randomly evict an entry in the set if the set if full.
  struct CacheEntry *evictEntry(MDInstrAddr IA) {
    unsigned setIndex = getSetIndex(IA);

    // Attempt to find an empty entry.
    // This will happen rarely (only for the first couple of accesses at program
    // startup) -- most of the time, we will evict.
    if(nextFree[setIndex] < numWays) {
      unsigned wayIndex = nextFree[setIndex];
      nextFree[setIndex]++;
      return &table[setIndex][wayIndex];
    } 
    
    // Evict the least recently used entry.
    struct CacheEntry *leastRecentEntry = nullptr;
    unsigned long long leastRecentTime = (unsigned long long)-1;
    for(auto &way: table[setIndex]) {
      if(way.lastAccess <= leastRecentTime) {
        leastRecentEntry = &way;
        leastRecentTime = way.lastAccess;
      }
    }
    return leastRecentEntry;
  }

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
