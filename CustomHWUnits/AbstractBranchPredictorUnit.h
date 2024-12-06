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

    enum BranchDirection {TAKEN, NOT_TAKEN};

    ~AbstractBranchPredictorUnit() {}
    /* This method is called by the FetchDelay stage after a branch was
     * executed to inform the branch predictor unit what path the execution
     * took (branch taken vs. not taken).
     * 
     * instrAddr: address of the branch instruction itself
     * nextInstrDirection: whether the branch was taken or not (i.e. 
     *                     TAKEN iff. nextInstrAddr == destAddr)
     */
    virtual void recordTakenBranch(MDInstrAddr instrAddr, BranchDirection nextInstrDirection) = 0;

    /* This method is called by the FetchDelay stage whenever it encounters
     * a branch instruction with metadata to attempt to predict a branching 
     * decision. A mispredict penalty will be added to the next instruction if
     * the BPU predicts wrong. */
    virtual BranchDirection predictBranch(MDInstrAddr instrAddr) = 0;

    virtual unsigned getMispredictionPenalty() = 0;

};

/* Similar to the AbstractBranchPredictorUnit, but it precdicts the branch
 * target address instead of the direction.
 */
class AbstractIndirectBranchPredictorUnit : public llvm::mca::HardwareUnit {
public:
    ~AbstractIndirectBranchPredictorUnit() {}
    virtual void recordTakenBranch(MDInstrAddr IA, MDInstrAddr destAddr) = 0;
    virtual MDInstrAddr predictBranch(MDInstrAddr IA) = 0;
    virtual unsigned getMispredictionPenalty() = 0;
};

}
}

#endif