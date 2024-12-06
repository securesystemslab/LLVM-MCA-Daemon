#ifndef LLVM_MCAD_TWO_BIT_LOCAL_BPU_H
#define LLVM_MCAD_TWO_BIT_LOCAL_BPU_H

#include "CustomHWUnits/AbstractBranchPredictorUnit.h"

#include <map>

#include "lib/gem5/sat_counter.h"
#include "lib/gem5/intmath.h"

namespace llvm {
namespace mcad {

/// @brief A simple two-bit local branch predictor.
///
/// This branch predictor is based off of the `LocalBP` in gem5.
/// It uses a table of n-bit saturating counters to predict whether a branch
/// will be taken or not.
class LocalBPU : public AbstractBranchPredictorUnit {
private:
  /// @brief The penalty for a misprediction.
  const unsigned mispredictionPenalty;
  /// @brief The number of control bits per entry in the predictor table.
  const unsigned numCtrlBits;
  /// @brief The size of the predictor table in bits.
  const unsigned predictorSize;
  /// @brief The number of entries in the predictor table.
  const unsigned numPredictorSets;
  /// @brief The branch predictor table with n-bit saturating counters as entries.
  std::vector<gem5::SatCounter8> predictorTable;

public:
  LocalBPU(unsigned mispredictionPenalty = 20, unsigned numCtrlBits = 2,
           unsigned predictorSize = 2048)
      : mispredictionPenalty(mispredictionPenalty),
        numCtrlBits(numCtrlBits), predictorSize(predictorSize)
        ,numPredictorSets(predictorSize / numCtrlBits)
        ,predictorTable(numPredictorSets, gem5::SatCounter8(numCtrlBits))
      {
        assert(gem5::isPowerOf2(predictorSize));
        assert(numCtrlBits > 0);
      };

  void recordTakenBranch(MDInstrAddr IA, BranchDirection nextInstrDirection) override;
  BranchDirection predictBranch(MDInstrAddr IA) override;
  unsigned getMispredictionPenalty() override { return mispredictionPenalty; }

private:
  /// @brief Get the index of the predictor table for the given instruction address.
  unsigned getPredictorIndex(MDInstrAddr IA) const;
  /// @brief Get the prediction for the given instruction address.
  bool getPrediction(MDInstrAddr IA) const;
};

} // namespace mcad
} // namespace llvm

#endif /* LLVM_MCAD_TWO_BIT_LOCAL_BPU_H */
