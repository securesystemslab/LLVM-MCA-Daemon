#include <map>
#include "CustomHWUnits/NaiveBranchPredictorUnit.h"

namespace llvm {
namespace mcad {

void NaiveBranchPredictorUnit::recordTakenBranch(MDInstrAddr instrAddr, BranchDirection nextInstrDirection) {
    // If no entry exists yet, add one
    if(branchHistory.find(instrAddr) == branchHistory.end()) {
        branchHistory[instrAddr] = {};
    }
    // Update the entry
    branchHistory[instrAddr].lastDirection = nextInstrDirection;
    branchHistory[instrAddr].lastUse = nAccesses;

    // Evict the least recently used entry if we are at capacity
    if(branchHistory.size() >= historyCapacity) {
        auto min_it = branchHistory.begin();
        for(auto it = branchHistory.begin(); it != branchHistory.end(); it++) {
            if(it->second.lastUse < min_it->second.lastUse) {
                min_it = it;
            }
        }
        branchHistory.erase(min_it);
    }
}

AbstractBranchPredictorUnit::BranchDirection NaiveBranchPredictorUnit::predictBranch(MDInstrAddr instrAddr) {
    ++nAccesses;
    if(branchHistory.find(instrAddr) != branchHistory.end()) {
        branchHistory[instrAddr].lastUse = nAccesses;
        return branchHistory[instrAddr].lastDirection;
    }
    // We have no history on this; predict a fall-through branch
    return NOT_TAKEN;
}

}
}
