#ifndef LLVM_MCAD_BROKERS_BROKER_H
#define LLVM_MCAD_BROKERS_BROKER_H
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInst.h"
#include <utility>

namespace llvm {
namespace mca {
class MetadataRegistry;
}

namespace mcad {
struct MDExchanger {
  mca::MetadataRegistry &MDRegistry;
  // Mapping MCInst to index in MetadataRegistry
  DenseMap<const MCInst*, unsigned> IndexMap;

  explicit MDExchanger(mca::MetadataRegistry &MDR)
    : MDRegistry(MDR) {}
};

// A simple interface for MCAWorker to fetch next MCInst
//
// Currently it's totally up to the Brokers to control their
// lifecycle. Client of this interface only cares about MCInsts.
struct Broker {
  // Broker should own the MCInst so only return the pointer
  //
  // Fetch MCInsts in batch. Size is the desired number of MCInsts
  // requested by the caller. When it's -1 the Broker will put until MCIS
  // is full. Of course, it's totally possible that Broker will only give out
  // MCInsts that are less than Size.
  // Note that for performance reason we're using mutable ArrayRef so the caller
  // should supply a fixed size array. And the Broker will always write from
  // index 0.
  // Return the number of MCInst put into the buffer, or -1 if no MCInst left
  virtual int fetch(MutableArrayRef<const MCInst*> MCIS, int Size = -1,
                    MDExchanger *MDE = nullptr) {
    return -1;
  }

  // Region is similar to `CodeRegion` in the original llvm-mca. Basically
  // MCAD will create a separate MCA pipeline to analyze each Region.
  // If a Broker supports Region this method should return true and MCAD will
  // use `fetchRegion` method instead.
  virtual bool hasRegionFeature() const { return false; }

  struct RegionDescriptor {
    bool IsEnd;
    llvm::StringRef Description;

    explicit RegionDescriptor(bool End, llvm::StringRef Text = "")
      : IsEnd(End), Description(Text) {}

    // Return true if it's the end of a region
    inline operator bool() const { return IsEnd; }
  };

  // Similar to `fetch`, but returns the number of MCInst fetched and whether
  // the last element in MCIS is also the last instructions in the current Region.
  // Note that MCIS always aligned with the boundary of Region (i.e. the last
  // instruction of a Region will not be in the middle of MCIS)
  virtual std::pair<int, RegionDescriptor>
  fetchRegion(MutableArrayRef<const MCInst*> MCIS, int Size = -1,
              MDExchanger *MDE = nullptr) {
    return std::make_pair(-1, RegionDescriptor(true));
  }

  virtual ~Broker() {}
};
} // end namespace mcad
} // end namespace llvm
#endif
