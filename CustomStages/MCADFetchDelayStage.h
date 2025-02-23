// This class does not model a real hardware stage. It is used to block the
// pipeline for a number of cycles to prevent further instructions from being
// fetched. We use this to model the cost of branch mispredictions.

#ifndef LLVM_MCAD_FETCH_DELAY_STAGE_H
#define LLVM_MCAD_FETCH_DELAY_STAGE_H

#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MCA/SourceMgr.h"
#include "llvm/MCA/Stages/Stage.h"
#include "CustomHWUnits/AbstractBranchPredictorUnit.h"
#include "CustomHWUnits/Cache.h"
#include "MetadataRegistry.h"
#include "Statistics.h"

#include <vector>
#include <queue>
#include <optional>

namespace llvm {
namespace mcad {

class MCADFetchDelayStage : public llvm::mca::Stage {

    struct DelayedInstr {
        unsigned delayCyclesLeft;
        llvm::mca::InstRef IR;
    };

    const llvm::MCInstrInfo &MCII;
    std::deque<DelayedInstr> instrQueue = {};

    AbstractBranchPredictorUnit *BPU;
    MetadataRegistry &MD;

    // Whenever a branch instruction is executed, we run the branch predictor 
    // and store the predicted branch direction here.
    // At the next instruction, we compare the predicted direction to the actual
    // direction taken (fallthrough vs. branch taken) and add a penalty if 
    // there is a mismatch.
    // Non-branch instructions set this member to nullopt.
    std::optional<AbstractBranchPredictorUnit::BranchDirection> predictedBranchDirection = std::nullopt;
    
    // Stores the address and size of the last executed instruction.
    std::optional<MDInstrAddr> previousInstrAddr = std::nullopt;
    std::optional<unsigned> previousInstrSize = std::nullopt;

    // This limits the number of cycles that the FetchDelay stage will simulate
    // a stalled fetch unit (-1 = unlimited, 0 = skip any cycles where fetch
    // stage would be stalled). This improves MCAD runtime (fewer cycles need
    // to be simulated) but it also reduces accuracy (other units may still
    // perform useful work when the fetch stage is stalled).
    int maxNumberSimulatedStallCycles = -1;

public: 

    // The memory cache unit.
    std::optional<CacheUnit> CU = std::nullopt;

    struct Statistics {
        OverflowableCount numSkippedDelayCycles = {};
        OverflowableCount numBranches = {};
        OverflowableCount numMispredictions = {};
        OverflowableCount numInstrLoadCycles = {};
    };

    Statistics stats = {};

public:
    MCADFetchDelayStage(const llvm::MCInstrInfo &MCII, MetadataRegistry &MD,
                        AbstractBranchPredictorUnit *BPU,
                        std::optional<CacheUnit> CU = std::nullopt,
                        int maxNumberSimulatedStallCycles = -1)
        : MCII(MCII), MD(MD), BPU(BPU), CU(std::move(CU)), maxNumberSimulatedStallCycles(maxNumberSimulatedStallCycles) {}

    bool hasWorkToComplete() const override;
    bool isAvailable(const llvm::mca::InstRef &IR) const override;
    llvm::Error execute(llvm::mca::InstRef &IR) override;

    llvm::Error cycleStart() override;

    llvm::Error forwardDueInstrs();

    bool enableInstructionCacheModeling = true;
    bool enableBranchPredictorModeling = true;

};

}
}

#endif