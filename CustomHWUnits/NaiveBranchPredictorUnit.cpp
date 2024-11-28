#include <map>
#include "CustomHWUnits/NaiveBranchPredictorUnit.h"

namespace llvm {
namespace mcad {

void NaiveBranchPredictorUnit::recordTakenBranch(MDInstrAddr instrAddr, BranchDirection nextInstrDirection) {
    branchHistory[instrAddr] = nextInstrDirection;
}

AbstractBranchPredictorUnit::BranchDirection NaiveBranchPredictorUnit::predictBranch(MDInstrAddr instrAddr) {
    if(branchHistory.find(instrAddr) != branchHistory.end()) {
        return branchHistory[instrAddr];
    }
    // We have no history on this; predict a fall-through branch
    return NOT_TAKEN;
}

}
}
