#include "llvm/ADT/StringExtras.h"
#include "llvm/MCA/Instruction.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include "CustomHWUnits/MCADLSUnit.h"
#include "MetadataCategories.h"

#define DEBUG_TYPE "llvm-mca"

namespace llvm {
namespace mcad {

uint64_t MDMemoryAccess::getExtendedStartAddr() const {
  if (LLVM_UNLIKELY(BundledMAs))
    return BundledMAs->ExtendedAddr;
  else
    return Addr;
}

uint64_t MDMemoryAccess::getExtendedEndAddr() const {
  if (LLVM_UNLIKELY(BundledMAs))
    return BundledMAs->ExtendedAddr + BundledMAs->ExtendedSize;
  else
    return Addr + Size;
}

void MDMemoryAccess::append(bool NewIsStore, uint64_t NewAddr,
                            unsigned NewSize) {
  if (!BundledMAs)
    BundledMAs = std::make_shared<BundledMemoryAccesses>(Addr, Size);
  auto &BMA = *BundledMAs;

  if (NewAddr < BMA.ExtendedAddr)
    BMA.ExtendedAddr = NewAddr;

  uint64_t NewEnd = NewAddr + NewSize;
  if (NewEnd > BMA.ExtendedAddr + BMA.ExtendedSize)
    BMA.ExtendedSize = NewEnd - BMA.ExtendedAddr;

  BMA.Accesses.emplace_back(NewIsStore, NewAddr, NewSize);
}

#ifndef NDEBUG
raw_ostream &operator<<(raw_ostream &OS, const MDMemoryAccess &MDA) {
  OS << "[ " << format_hex(MDA.Addr, 16) << " - "
     << format_hex(uint64_t(MDA.Addr + MDA.Size), 16) << " ], ";
  OS << "IsStore: " << toStringRef(MDA.IsStore);
  return OS;
}
#endif

std::optional<MDMemoryAccess>
MCADLSUnit::getMemoryAccessMD(const mca::InstRef &IR) const {
  auto Id = IR.getInstruction()->getIdentifier();
  if (MDRegistry && Id.has_value()) {
    auto &Registry = (*MDRegistry)[llvm::mcad::MD_LSUnit_MemAccess];
    return Registry.get<MDMemoryAccess>(*Id);
  }
  return std::nullopt;
}

bool MCADLSUnit::noAlias(unsigned GID,
                         const std::optional<MDMemoryAccess> &MDA) const {
  if (MDA) {
    const CustomMemoryGroup &MG = getCustomGroup(GID);
    LLVM_DEBUG(dbgs() << "[LSUnit][MD]: Comparing GID " << GID
                      << " with MDMemoryAccess " << *MDA << "\n");
    bool Result = !MG.isMemAccessAlias(*MDA);
    LLVM_DEBUG(dbgs() << "[LSUnit][MD]: GID is alias with MDA: "
                      << toStringRef(!Result) << "\n");
    if (!Result) {
      LLVM_DEBUG(dbgs() << "[LSUnit] We have alias!\n");
    }
    return Result;
  } else
    return assumeNoAlias();
}

unsigned MCADLSUnit::dispatch(const mca::InstRef &IR) {
  const mca::Instruction &IS = *IR.getInstruction();
  auto MaybeMDA = getMemoryAccessMD(IR);

  bool IsStoreBarrier = IS.isAStoreBarrier();
  bool IsLoadBarrier = IS.isALoadBarrier();
  assert((IS.getMayLoad() || IS.getMayStore()) && "Not a memory operation!");

  if (IS.getMayLoad())
    acquireLQSlot();
  if (isStore(IS, MaybeMDA))
    acquireSQSlot();

  if (isStore(IS, MaybeMDA)) {
    unsigned NewGID = createCustomMemoryGroup();
    CustomMemoryGroup &NewGroup = getCustomGroup(NewGID);
    NewGroup.addInstruction();
    NewGroup.addMemAccess(MaybeMDA);
    LLVM_DEBUG(if (MaybeMDA) dbgs()
               << "[LSUnit][MD]: GID " << NewGID
               << " has a new MemAccessMD: " << *MaybeMDA << "\n");

    // A store may not pass a previous load or load barrier.
    unsigned ImmediateLoadDominator =
        std::max(CurrentLoadGroupID, CurrentLoadBarrierGroupID);
    if (ImmediateLoadDominator) {
      CustomMemoryGroup &IDom = getCustomGroup(ImmediateLoadDominator);
      LLVM_DEBUG(dbgs() << "[LSUnit]: GROUP DEP: (" << ImmediateLoadDominator
                        << ") --> (" << NewGID << ")\n");
      IDom.addSuccessor(&NewGroup, !noAlias(ImmediateLoadDominator, MaybeMDA));
    }

    // A store may not pass a previous store barrier.
    if (CurrentStoreBarrierGroupID) {
      CustomMemoryGroup &StoreGroup =
          getCustomGroup(CurrentStoreBarrierGroupID);
      LLVM_DEBUG(dbgs() << "[LSUnit]: GROUP DEP: ("
                        << CurrentStoreBarrierGroupID << ") --> (" << NewGID
                        << ")\n");
      StoreGroup.addSuccessor(&NewGroup, true);
    }

    // A store may not pass a previous store.
    if (CurrentStoreGroupID &&
        (CurrentStoreGroupID != CurrentStoreBarrierGroupID)) {
      CustomMemoryGroup &StoreGroup = getCustomGroup(CurrentStoreGroupID);
      LLVM_DEBUG(dbgs() << "[LSUnit]: GROUP DEP: (" << CurrentStoreGroupID
                        << ") --> (" << NewGID << ")\n");
      StoreGroup.addSuccessor(&NewGroup,
                              !noAlias(CurrentStoreGroupID, MaybeMDA));
    }

    CurrentStoreGroupID = NewGID;
    if (IsStoreBarrier)
      CurrentStoreBarrierGroupID = NewGID;

    if (IS.getMayLoad()) {
      CurrentLoadGroupID = NewGID;
      if (IsLoadBarrier)
        CurrentLoadBarrierGroupID = NewGID;
    }

    return NewGID;
  }

  assert(IS.getMayLoad() && "Expected a load!");

  unsigned ImmediateLoadDominator =
      std::max(CurrentLoadGroupID, CurrentLoadBarrierGroupID);

  // A new load group is created if we are in one of the following situations:
  // 1) This is a load barrier (by construction, a load barrier is always
  //    assigned to a different memory group).
  // 2) There is no load in flight (by construction we always keep loads and
  //    stores into separate memory groups).
  // 3) There is a load barrier in flight. This load depends on it.
  // 4) There is an intervening store between the last load dispatched to the
  //    LSU and this load. We always create a new group even if this load
  //    does not alias the last dispatched store.
  // 5) There is no intervening store and there is an active load group.
  //    However that group has already started execution, so we cannot add
  //    this load to it.
  bool ShouldCreateANewGroup =
      IsLoadBarrier || !ImmediateLoadDominator ||
      CurrentLoadBarrierGroupID == ImmediateLoadDominator ||
      ImmediateLoadDominator <= CurrentStoreGroupID ||
      getGroup(ImmediateLoadDominator).isExecuting();

  if (ShouldCreateANewGroup) {
    unsigned NewGID = createCustomMemoryGroup();
    CustomMemoryGroup &NewGroup = getCustomGroup(NewGID);
    NewGroup.addInstruction();
    NewGroup.addMemAccess(MaybeMDA);
    LLVM_DEBUG(if (MaybeMDA) dbgs()
               << "[LSUnit][MD]: GID " << NewGID
               << " has a new MemAccessMD: " << *MaybeMDA << "\n");

    // A load may not pass a previous store or store barrier
    // unless flag 'NoAlias' is set.
    if (CurrentStoreGroupID && !noAlias(CurrentStoreGroupID, MaybeMDA)) {
      CustomMemoryGroup &StoreGroup = getCustomGroup(CurrentStoreGroupID);
      LLVM_DEBUG(dbgs() << "[LSUnit]: GROUP DEP: (" << CurrentStoreGroupID
                        << ") --> (" << NewGID << ")\n");
      StoreGroup.addSuccessor(&NewGroup, true);
    }

    // A load barrier may not pass a previous load or load barrier.
    if (IsLoadBarrier) {
      if (ImmediateLoadDominator) {
        CustomMemoryGroup &LoadGroup = getCustomGroup(ImmediateLoadDominator);
        LLVM_DEBUG(dbgs() << "[LSUnit]: GROUP DEP: (" << ImmediateLoadDominator
                          << ") --> (" << NewGID << ")\n");
        LoadGroup.addSuccessor(&NewGroup, true);
      }
    } else {
      // A younger load cannot pass a older load barrier.
      if (CurrentLoadBarrierGroupID) {
        CustomMemoryGroup &LoadGroup =
            getCustomGroup(CurrentLoadBarrierGroupID);
        LLVM_DEBUG(dbgs() << "[LSUnit]: GROUP DEP: ("
                          << CurrentLoadBarrierGroupID << ") --> (" << NewGID
                          << ")\n");
        LoadGroup.addSuccessor(&NewGroup, true);
      }
    }

    CurrentLoadGroupID = NewGID;
    if (IsLoadBarrier)
      CurrentLoadBarrierGroupID = NewGID;
    return NewGID;
  }

  // A load may pass a previous load.
  CustomMemoryGroup &Group = getCustomGroup(CurrentLoadGroupID);
  Group.addInstruction();
  Group.addMemAccess(MaybeMDA);
  LLVM_DEBUG(if (MaybeMDA) dbgs()
             << "[LSUnit][MD]: GID " << CurrentLoadGroupID
             << " has a new MemAccessMD: " << *MaybeMDA << "\n");
  return CurrentLoadGroupID;
}

MCADLSUnit::Status MCADLSUnit::isAvailable(const mca::InstRef &IR) const {
  const mca::Instruction &IS = *IR.getInstruction();
  auto MaybeMDA = getMemoryAccessMD(IR);
  if (IS.getMayLoad() && isLQFull())
    return MCADLSUnit::LSU_LQUEUE_FULL;
  if (isStore(IS, MaybeMDA) && isSQFull())
    return MCADLSUnit::LSU_SQUEUE_FULL;
  return MCADLSUnit::LSU_AVAILABLE;
}

void MCADLSUnit::onInstructionRetired(const mca::InstRef &IR) {
  const mca::Instruction &IS = *IR.getInstruction();
  bool IsALoad = IS.getMayLoad();
  auto MaybeMDA = getMemoryAccessMD(IR);
  bool IsAStore = isStore(IS, MaybeMDA);
  assert((IsALoad || IsAStore) && "Expected a memory operation!");

  if (IsALoad) {
    releaseLQSlot();
    LLVM_DEBUG(dbgs() << "[LSUnit]: Instruction idx=" << IR.getSourceIndex()
                      << " has been removed from the load queue.\n");
  }

  if (IsAStore) {
    releaseSQSlot();
    LLVM_DEBUG(dbgs() << "[LSUnit]: Instruction idx=" << IR.getSourceIndex()
                      << " has been removed from the store queue.\n");
  }
}

void MCADLSUnit::onInstructionExecuted(const mca::InstRef &IR) {
  const mca::Instruction &IS = *IR.getInstruction();
  if (!IS.isMemOp())
    return;
  unsigned GroupID = IS.getLSUTokenID();
  auto It = CustomGroups.find(GroupID);
  assert(It != CustomGroups.end() && "Instruction not dispatched to the LS unit");
  It->second->onInstructionExecuted(IR);
  if (It->second->isExecuted())
    CustomGroups.erase(It);
  if (!isValidGroupID(GroupID)) {
    if (GroupID == CurrentLoadGroupID)
      CurrentLoadGroupID = 0;
    if (GroupID == CurrentStoreGroupID)
      CurrentStoreGroupID = 0;
    if (GroupID == CurrentLoadBarrierGroupID)
      CurrentLoadBarrierGroupID = 0;
    if (GroupID == CurrentStoreBarrierGroupID)
      CurrentStoreBarrierGroupID = 0;
  }
}

} // namespace mcad
} // namespace llvm
