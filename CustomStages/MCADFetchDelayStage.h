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

    // The memory cache unit.
    std::optional<CacheUnit> CU = std::nullopt;
    
    // Stores the address and size of the last executed instruction.
    std::optional<MDInstrAddr> previousInstrAddr = std::nullopt;
    std::optional<unsigned> previousInstrSize = std::nullopt;

public: 
    // Stats
    // TODO: Move these elsewhere, as they are useful outside of just branch
    // prediction or the FetchDelayStage; we could also make use of the event
    // infrastructure that already exists (grep for STALL event)
    struct OverflowableCount {
        unsigned long long count;
        bool overflowed;
        void inc() {
            if(count + 1 < count) {
                overflowed = true;
            }
            count++;
        }
    };

    struct Statistics {
        OverflowableCount numBranches = {};
        OverflowableCount numMispredictions = {};
    };

    Statistics stats = {};

public:
    MCADFetchDelayStage(const llvm::MCInstrInfo &MCII, MetadataRegistry &MD,
                        AbstractBranchPredictorUnit *BPU,
                        std::optional<CacheUnit> CU = std::nullopt)
        : MCII(MCII), MD(MD), BPU(BPU), CU(std::move(CU)) {}

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