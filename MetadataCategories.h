#ifndef LLVM_MCA_METADATACATEGORIES_H
#define LLVM_MCA_METADATACATEGORIES_H


#include "MetadataRegistry.h"
#include "llvm/MCA/Instruction.h"

namespace llvm {
namespace mcad {

enum MetadataCategories {

// Metadata for LSUnit
MD_LSUnit_MemAccess = 0,

// Metadata for Branch Prediction
MD_FrontEnd_BranchFlow = 1,

// Virtual address at which an instruction is located in memory
MD_InstrAddr = 2,

// Used for marking the start of custom MD Category
MD_LAST,

// Metadata categories (custom)
MD_BinaryRegionMarkers

};

struct MDInstrAddr {
    unsigned long long addr;
    unsigned size;

    const bool operator<(const MDInstrAddr &b) const {
        return addr + size < b.addr;
    }
    const bool operator==(const MDInstrAddr &b) const {
        return addr == b.addr && size == b.size;
    }
    const bool operator!=(const MDInstrAddr &b) const {
        return addr != b.addr || size != b.size;
    }
};

std::optional<MDInstrAddr> getMDInstrAddrForInstr(MetadataRegistry &MD, const llvm::mca::InstRef &IR);

} // end namespace mcad
} // end namespace llvm
#endif
