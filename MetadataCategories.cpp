#include "llvm/MCA/Instruction.h"
#include <optional>
#include "MetadataRegistry.h"
#include "MetadataCategories.h"

namespace llvm {
namespace mcad {

std::optional<MDInstrAddr> getMDInstrAddrForInstr(MetadataRegistry &MD, const llvm::mca::InstRef &IR) {
    const llvm::mca::Instruction *I = IR.getInstruction();
    auto instrId = I->getIdentifier();
    if (instrId.has_value()) {
        auto &Registry = MD[llvm::mcad::MD_InstrAddr];
        auto instrAddr = Registry.get<MDInstrAddr>(*instrId);
        return instrAddr;
    }
    return std::nullopt;
}

}
}