#ifndef LLVM_MCA_METADATACATEGORIES_H
#define LLVM_MCA_METADATACATEGORIES_H
namespace llvm {
namespace mcad {
// Metadata for LSUnit
static constexpr unsigned MD_LSUnit_MemAccess = 0;

// Metadata for Branch Prediction
static constexpr unsigned MD_FrontEnd_BranchFlow = 1;

// Used for marking the start of custom MD Category
static constexpr unsigned MD_LAST = MD_FrontEnd_BranchFlow;

// Metadata categories (custom)
static constexpr unsigned MD_BinaryRegionMarkers = MD_LAST + 1;
} // end namespace mcad
} // end namespace llvm
#endif
