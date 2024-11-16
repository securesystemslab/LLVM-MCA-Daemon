// This class does not model a real hardware stage. It is used to block the
// pipeline for a number of cycles to prevent further instructions from being
// fetched. We use this to model the cost of branch mispredictions.

#ifndef LLVM_MCAD_FETCH_DELAY_STAGE_H
#define LLVM_MCAD_FETCH_DELAY_STAGE_H

#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MCA/SourceMgr.h"
#include "llvm/MCA/Stages/Stage.h"

#include <vector>
#include <queue>

namespace llvm {
namespace mcad {

class MCADFetchDelayStage : public llvm::mca::Stage {

    struct DelayedInstr {
        unsigned delayCyclesLeft;
        llvm::mca::InstRef IR;
    };

    const llvm::MCInstrInfo &MCII;
    std::deque<DelayedInstr> instrQueue = {};

public:
    MCADFetchDelayStage(const llvm::MCInstrInfo &MCII) : MCII(MCII) {}

    bool hasWorkToComplete() const override;
    bool isAvailable(const llvm::mca::InstRef &IR) const override;
    llvm::Error execute(llvm::mca::InstRef &IR) override;

    //llvm::Error cycleStart() override;
    llvm::Error cycleStart() override;

    llvm::Error forwardDueInstrs();

    ///// Called after the pipeline is resumed from pausing state.
    //virtual Error cycleResume() { return ErrorSuccess(); }

    ///// Called once at the end of each cycle.

};

}
}

#endif