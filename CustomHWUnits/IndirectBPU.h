#ifndef LLVM_MCAD_INDIRECT_BPU_H
#define LLVM_MCAD_INDIRECT_BPU_H

#include "CustomHWUnits/AbstractBranchPredictorUnit.h"

#include <map>

#include "lib/gem5/sat_counter.h"
#include "lib/gem5/intmath.h"

namespace llvm {
namespace mcad {

/// @brief A simple indirect branch predictor.
///
/// This branch predictor is based off of the `IndirectBP` in gem5.
/// It maintains a set-associative table of indirect branch targets.
class IndirectBPU : public AbstractIndirectBranchPredictorUnit {
private:
    const unsigned numSets;
    const unsigned numWays;

    struct IndirectBranchEntry {
        MDInstrAddr tag = {0};
        MDInstrAddr target = {0};
    };

    std::vector<std::vector<IndirectBranchEntry>> indirectBranchTable;

    unsigned getSetIndex(MDInstrAddr IA) const;


public:
  IndirectBPU(unsigned numSets = 256, unsigned numWays = 2) 
  : numSets(numSets), numWays(numWays), indirectBranchTable(numSets, std::vector<IndirectBranchEntry>(numWays)) {}

  void recordTakenBranch(MDInstrAddr IA, MDInstrAddr destAddr) override;
  MDInstrAddr predictBranch(MDInstrAddr IA) override;
};

} // namespace mcad
} // namespace llvm

#endif /* LLVM_MCAD_INDIRECT_BPU_H */
