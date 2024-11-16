#ifndef LLVM_MCA_METADATACATEGORIES_H
#define LLVM_MCA_METADATACATEGORIES_H
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

} // end namespace mcad
} // end namespace llvm
#endif
