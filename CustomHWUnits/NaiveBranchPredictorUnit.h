#ifndef LLVM_MCAD_NAIVE_BRANCH_PREDICTOR_UNIT_H
#define LLVM_MCAD_NAIVE_BRANCH_PREDICTOR_UNIT_H

#include <map>
#include "CustomHWUnits/AbstractBranchPredictorUnit.h"

namespace llvm {
namespace mcad {

class NaiveBranchPredictorUnit : public AbstractBranchPredictorUnit {

    struct HistoryEntry {
        AbstractBranchPredictorUnit::BranchDirection lastDirection;
        unsigned lastUse;
    };

    unsigned mispredictionPenalty;
    unsigned historyCapacity;
    unsigned nAccesses;
    std::map<MDInstrAddr, struct HistoryEntry> branchHistory = {};

public:
    NaiveBranchPredictorUnit(unsigned mispredictionPenalty, unsigned branchHistoryTableSize)
     : mispredictionPenalty(mispredictionPenalty),
       historyCapacity(branchHistoryTableSize) { };

    void recordTakenBranch(MDInstrAddr instrAddr, BranchDirection nextInstrDirection) override;

    BranchDirection predictBranch(MDInstrAddr instrAddr) override;

    unsigned getMispredictionPenalty() override {
        return mispredictionPenalty;
    }

};

}
}

#endif