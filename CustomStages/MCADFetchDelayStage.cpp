#include <iostream>
#include "CustomStages/MCADFetchDelayStage.h"

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
    if(MCID.isBranch()) {
        // delayed, will have to wait
        delayCyclesLeft = 100;
    }
    instrQueue.emplace_back(DelayedInstr { delayCyclesLeft, IR });
    // if the instruction is not delayed, execute it immediately (it will
    // have a delayCyclesLeft of 0 and be at the top of the queue)
    return forwardDueInstrs();
}

llvm::Error MCADFetchDelayStage::cycleStart() {
    if(!instrQueue.empty()) {
        instrQueue.front().delayCyclesLeft--;
    }
    return forwardDueInstrs();
}

}
}
