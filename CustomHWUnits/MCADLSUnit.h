#ifndef LLVM_MCAD_CUSTOMHWUNITS_LSUNIT_H
#define LLVM_MCAD_CUSTOMHWUNITS_LSUNIT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MCA/HardwareUnits/HardwareUnit.h"
#include "llvm/MCA/HardwareUnits/LSUnit.h"
#include "llvm/MCA/Instruction.h"
#include <set>

#include "CustomHWUnits/CustomSourceMgr.h"
#include "MetadataRegistry.h"

namespace llvm {
namespace mcad {

/// Metadata structure for memory access.
/// This structure can be copied by value, however it's not
/// _trivially_ copyable.
struct MDMemoryAccess {
  bool IsStore;
  uint64_t Addr;
  unsigned Size;

  struct BundledMemoryAccesses;
  std::shared_ptr<BundledMemoryAccesses> BundledMAs;

  MDMemoryAccess(bool ST, uint64_t ADR, unsigned SZ,
                 BundledMemoryAccesses *BMA = nullptr)
      : IsStore(ST), Addr(ADR), Size(SZ), BundledMAs(BMA) {}

  // Upper bound address of all the memory accesses here
  uint64_t getExtendedStartAddr() const;

  // Lower bound address of all the memory accesses here
  uint64_t getExtendedEndAddr() const;

  // Append a memory access to the bundle
  void append(bool IsStore, uint64_t Addr, unsigned Size);
};

/// Bundled memory accesses are memory accesses performed in the same
/// MCInst. Note that `Accesses` should preserve the order of the original
/// accesses.
struct MDMemoryAccess::BundledMemoryAccesses {
  // Upper bound address of the bundled memory accesses
  uint64_t ExtendedAddr;
  // Lower bound address of the bundled memory accesses
  unsigned ExtendedSize;
  SmallVector<MDMemoryAccess, 2> Accesses;

  BundledMemoryAccesses(uint64_t OriginalAddr, unsigned OriginalSize)
      : ExtendedAddr(OriginalAddr), ExtendedSize(OriginalSize) {}
};

inline bool operator<(const MDMemoryAccess &LHS, const MDMemoryAccess &RHS) {
  // FIXME: Since it's totally possible all (bundled) memory accesses are
  // sparse, this comparison can only give you sound but not precise result
  // (extremely imprecise if the density is low). In order to yield a precise
  // comparison, one might need to sort all the bundled memory accesses before
  // comparing them, which is expensive.
  return LHS.getExtendedEndAddr() <= RHS.getExtendedStartAddr();
}
#ifndef NDEBUG
raw_ostream &operator<<(raw_ostream &OS, const MDMemoryAccess &MDA);
#endif

class CustomMemoryGroup : public mca::MemoryGroup {
  std::multiset<MDMemoryAccess> MDMemAccesses;

  CustomMemoryGroup(const CustomMemoryGroup &) = delete;
  CustomMemoryGroup &operator=(const CustomMemoryGroup &) = delete;

public:
  CustomMemoryGroup() = default;
  CustomMemoryGroup(CustomMemoryGroup &&) = default;

  void addMemAccess(const std::optional<MDMemoryAccess> &MaybeMDA) {
    if (MaybeMDA)
      MDMemAccesses.insert(*MaybeMDA);
  }
  bool isMemAccessAlias(const MDMemoryAccess &MDA) const {
    return MDMemAccesses.count(MDA);
  }
};

class MCADLSUnit : public mca::LSUnit {
  unsigned CurrentLoadGroupID;
  unsigned CurrentLoadBarrierGroupID;
  unsigned CurrentStoreGroupID;
  unsigned CurrentStoreBarrierGroupID;

  DenseMap<unsigned, std::unique_ptr<CustomMemoryGroup>> CustomGroups;
  unsigned NextCustomGroupID;

  CustomSourceMgr &SrcMgr;

  MetadataRegistry *MDRegistry;

public:
  MCADLSUnit(const MCSchedModel &SM, CustomSourceMgr &SrcMgr,
             MetadataRegistry *MDR)
      : LSUnit(SM, /* LQSize */ 0, /* SQSize */ 0, /* NoAlias */ false),
        SrcMgr(SrcMgr), MDRegistry(MDR) {}
  MCADLSUnit(const MCSchedModel &SM, unsigned LQ, unsigned SQ,
             CustomSourceMgr &SrcMgr, MetadataRegistry *MDR)
      : LSUnit(SM, LQ, SQ, /* NoAlias */ false), SrcMgr(SrcMgr),
        MDRegistry(MDR) {}
  MCADLSUnit(const MCSchedModel &SM, unsigned LQ, unsigned SQ,
             bool AssumeNoAlias, CustomSourceMgr &SrcMgr, MetadataRegistry *MDR)
      : LSUnit(SM, LQ, SQ, AssumeNoAlias), CurrentLoadGroupID(0),
        CurrentLoadBarrierGroupID(0), CurrentStoreGroupID(0),
        CurrentStoreBarrierGroupID(0), SrcMgr(SrcMgr), MDRegistry(MDR) {}

  Status isAvailable(const mca::InstRef &IR) const override;

  unsigned dispatch(const mca::InstRef &IR) override;

  bool isValidGroupID(unsigned Index) const {
    return Index && CustomGroups.contains(Index);
  }

  bool isReady(const mca::InstRef &IR) const {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    const CustomMemoryGroup &Group = getCustomGroup(GroupID);
    return Group.isReady();
  }

  bool isPending(const mca::InstRef &IR) const {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    const CustomMemoryGroup &Group = getCustomGroup(GroupID);
    return Group.isPending();
  }

  bool isWaiting(const mca::InstRef &IR) const {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    const CustomMemoryGroup &Group = getCustomGroup(GroupID);
    return Group.isWaiting();
  }

  bool hasDependentUsers(const mca::InstRef &IR) const {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    const CustomMemoryGroup &Group = getCustomGroup(GroupID);
    return !Group.isExecuted() && Group.getNumSuccessors();
  }

  const CustomMemoryGroup &getCustomGroup(unsigned Index) const {
    assert(isValidGroupID(Index) && "Group doesn't exist!");
    return *CustomGroups.find(Index)->second;
  }

  CustomMemoryGroup &getCustomGroup(unsigned Index) {
    assert(isValidGroupID(Index) && "Group doesn't exist!");
    return *CustomGroups.find(Index)->second;
  }

  unsigned createCustomMemoryGroup() {
    CustomGroups.insert(std::make_pair(NextCustomGroupID,
                                       std::make_unique<CustomMemoryGroup>()));
    return NextCustomGroupID++;
  }

  bool noAlias(unsigned GID, const std::optional<MDMemoryAccess> &MDA) const;
  std::optional<MDMemoryAccess> getMemoryAccessMD(const mca::InstRef &IR) const;

  bool isStore(const mca::Instruction &IS,
               const std::optional<MDMemoryAccess> &MDA) const {
    if (MDA)
      return MDA->IsStore || IS.getMayStore();
    else
      return IS.getMayStore();
  }

  void onInstructionExecuted(const mca::InstRef &IR) override;
  void onInstructionRetired(const mca::InstRef &IR) override;

#ifndef NDEBUG
  void dump() const;
#endif
};

} // namespace mcad
} // namespace llvm

#endif // LLVM_MCAD_HARDWAREUNITS_LSUNIT_H
