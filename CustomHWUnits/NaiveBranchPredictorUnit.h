#ifndef LLVM_MCAD_NAIVE_BRANCH_PREDICTOR_UNIT_H
#define LLVM_MCAD_NAIVE_BRANCH_PREDICTOR_UNIT_H

#include <map>
#include "CustomHWUnits/AbstractBranchPredictorUnit.h"

namespace llvm {
namespace mcad {

class NaiveBranchPredictorUnit : public AbstractBranchPredictorUnit {
    unsigned mispredictionPenalty;
    std::map<MDInstrAddr, AbstractBranchPredictorUnit::BranchDirection> branchHistory = {};

public:
    NaiveBranchPredictorUnit(unsigned mispredictionPenalty = 20) : mispredictionPenalty(mispredictionPenalty) {};

    void recordTakenBranch(MDInstrAddr instrAddr, BranchDirection nextInstrDirection) override;

    BranchDirection predictBranch(MDInstrAddr instrAddr) override;

    unsigned getMispredictionPenalty() override {
        return mispredictionPenalty;
    }

};

}
}

#endif