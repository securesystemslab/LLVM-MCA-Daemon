#include "CustomStages/MCADFetchDelayStage.h"
#include "MetadataCategories.h"
#include "llvm/Support/Debug.h"
#define DEBUG_TYPE "llvm-mca"

#include <iostream>
#include <iomanip>
#include <optional>

namespace llvm {
namespace mcad {

struct MCADInstructionFetchedEvent {};

bool MCADFetchDelayStage::hasWorkToComplete() const {
    return !instrQueue.empty();
}

bool MCADFetchDelayStage::isAvailable(const llvm::mca::InstRef &IR) const {
    return checkNextStage(IR);
}

llvm::Error MCADFetchDelayStage::forwardDueInstrs() {
    while(!instrQueue.empty() && instrQueue.front().delayCyclesLeft == 0) {
        llvm::mca::InstRef IR = instrQueue.front().IR;
        if(!checkNextStage(IR)) {
            // Although this instruction has completed its delay cycles and is
            // ready to be forwarded, we have to keep holding it since the next
            // stage is not ready. Break out of the loop, because instructions
            // must be dispatched to the next stage from this one in-order.
            break;
        }
        if (llvm::Error Val = moveToTheNextStage(IR)) {
            return Val;
        }
        instrQueue.pop_front();
    }
    return llvm::ErrorSuccess();
}

llvm::Error MCADFetchDelayStage::execute(llvm::mca::InstRef &IR) {
    // We (ab-)use the LastGenericEventType to create a notification when the instruction first enters this stage.
    // We use this elsewhere to calculate the number of cycles between when an instruction first enters the pipeline and the end of its execution.
    notifyEvent<llvm::mca::HWInstructionEvent>(llvm::mca::HWInstructionEvent(llvm::mca::HWInstructionEvent::LastGenericEventType, IR));
    const llvm::mca::Instruction *I = IR.getInstruction();
    const llvm::mca::InstrDesc &ID = I->getDesc();
    const llvm::MCInstrDesc &MCID = MCII.get(I->getOpcode());
    bool immediatelyExecute = true;
    unsigned delayCyclesLeft = 0;
    std::optional<MDInstrAddr> instrAddr = getMDInstrAddrForInstr(MD, IR);
    std::optional<unsigned> instrSize = MCID.getSize(); 
    assert(MCID.getSize() > 0);

    // Check if previous instruction was a branch, and if so if the predicted
    // branch target matched what we ended up executing
    if(predictedBranchDirection.has_value() && instrAddr.has_value() && previousInstrAddr.has_value() && previousInstrSize.has_value()) {
        bool fellThrough = instrAddr->addr == (previousInstrAddr->addr + *previousInstrSize); 
        AbstractBranchPredictorUnit::BranchDirection actualBranchDirection = 
            (fellThrough ? AbstractBranchPredictorUnit::NOT_TAKEN 
                         : AbstractBranchPredictorUnit::TAKEN);
        BPU.recordTakenBranch(*previousInstrAddr, actualBranchDirection);

        if(actualBranchDirection != predictedBranchDirection) {
            // Previous prediction was wrong; this instruction will have extra
            // latency due to misprediction.
            delayCyclesLeft += BPU.getMispredictionPenalty();
            LLVM_DEBUG(dbgs() << "[MCAD FetchDelayStage] Previous branch at "); 
            LLVM_DEBUG(dbgs().write_hex(instrAddr->addr));
            LLVM_DEBUG(dbgs() << " mispredicted, delaying next instruction by " 
                       << delayCyclesLeft << " cycle(s).\n");
        } else {
            LLVM_DEBUG(dbgs() << "[MCAD FetchDelayStage] Previous branch at ");
            LLVM_DEBUG(dbgs().write_hex(instrAddr->addr));
            LLVM_DEBUG(dbgs() << " predicted correctly.\n" );
        }
    }
    // Update branch prediction state
    if(MCID.isBranch() && instrAddr.has_value()) {
        predictedBranchDirection = BPU.predictBranch(*instrAddr);
    } else {
        predictedBranchDirection = std::nullopt;
    }
    instrQueue.emplace_back(DelayedInstr { delayCyclesLeft, IR });
    previousInstrAddr = instrAddr;
    previousInstrSize = instrSize;
    // if the instruction is not delayed, execute it immediately (it will
    // have a delayCyclesLeft of 0 and be at the top of the queue)
    return forwardDueInstrs();
}

llvm::Error MCADFetchDelayStage::cycleStart() {
    if(!instrQueue.empty() && instrQueue.front().delayCyclesLeft > 0) {
        // An instruction may be at the front of the instruction queue with no
        // delay cycles left but have not been forwarded to the next stage yet
        // if the next stage is not ready yet.
        instrQueue.front().delayCyclesLeft--;
    }
    return forwardDueInstrs();
}

}
}
