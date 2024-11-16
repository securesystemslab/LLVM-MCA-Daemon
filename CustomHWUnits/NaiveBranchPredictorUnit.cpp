#include <map>
#include "CustomHWUnits/NaiveBranchPredictorUnit.h"

namespace llvm {
namespace mcad {

void NaiveBranchPredictorUnit::recordTakenBranch(MDInstrAddr IA, MDInstrAddr destAddr) {
    branchHistory[IA] = destAddr;
}

MDInstrAddr NaiveBranchPredictorUnit::predictBranch(MDInstrAddr IA) {
    if(branchHistory.find(IA) != branchHistory.end()) {
        return branchHistory[IA];
    }
    // We have no history on this; predict a fall-through branch
    // FIXME: fix this to use actual branch instruction size, which is likely
    // larger than one byte.
    return MDInstrAddr { IA.addr + 1 };
}

}
}
