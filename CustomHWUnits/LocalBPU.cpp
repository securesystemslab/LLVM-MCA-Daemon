#include <map>
#include "CustomHWUnits/LocalBPU.h"

namespace llvm {
namespace mcad {

void LocalBPU::recordTakenBranch(MDInstrAddr IA, BranchDirection nextInstrDirection) {
    bool isTaken = nextInstrDirection == BranchDirection::TAKEN;
    unsigned idx = getPredictorIndex(IA);
    predictorTable[idx] += isTaken;
}

BranchDirection LocalBPU::predictBranch(MDInstrAddr IA) {
    return getPrediction(IA) ? BranchDirection::TAKEN : BranchDirection::NOT_TAKEN;
}

unsigned LocalBPU::getPredictorIndex(MDInstrAddr IA) const {
    // TODO: this could probably be improved. gem5 shifts it by 2 then mask it.
    return IA.addr % numPredictorSets;
}

bool LocalBPU::getPrediction(MDInstrAddr IA) const {
    unsigned idx = getPredictorIndex(IA);
    // Return the MSB of the counter.
    return predictorTable[idx] >> (numCtrlBits - 1);
}
} // namespace mcad
} // namespace llvm
