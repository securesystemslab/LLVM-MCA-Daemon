#ifndef LLVM_MCAD_ABSTRACT_BRANCH_PREDICTOR_UNIT_H
#define LLVM_MCAD_ABSTRACT_BRANCH_PREDICTOR_UNIT_H

#include <optional>
#include "llvm/MCA/Instruction.h"
#include "llvm/MCA/HardwareUnits/HardwareUnit.h"
#include "MetadataRegistry.h"
#include "MetadataCategories.h"

namespace llvm {
namespace mcad {

class AbstractBranchPredictorUnit : public llvm::mca::HardwareUnit {

public:
    ~AbstractBranchPredictorUnit() {}
    virtual void recordTakenBranch(MDInstrAddr IA, MDInstrAddr destAddr) = 0;
    virtual MDInstrAddr predictBranch(MDInstrAddr IA) = 0;
    virtual unsigned getMispredictionPenalty() = 0;

};

}
}

#endif