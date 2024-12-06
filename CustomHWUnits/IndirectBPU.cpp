#include <map>
#include "CustomHWUnits/IndirectBPU.h"

namespace llvm {
namespace mcad {

void IndirectBPU::recordTakenBranch(MDInstrAddr IA, MDInstrAddr destAddr) {
    // Look for the entry in the table.
    auto& set = indirectBranchTable[getSetIndex(IA)];
    for (auto &way : set) {
        if (way.tag == IA) {
            way.target = destAddr;
            return;
        }
    }

    // If we didn't find an entry, we need to evict one.
    // We will choose a random entry to evict.
    auto &way = set[rand() % numWays];
    way.tag = IA;
    way.target = destAddr;
}

MDInstrAddr IndirectBPU::predictBranch(MDInstrAddr IA) {
    // Look for the entry in the table.
    auto& set = indirectBranchTable[getSetIndex(IA)];
    for (auto &way : set) {
        if (way.tag == IA) {
            return way.target;
        }
    }

    // If we didn't find an entry, we will predict the next byte.
    return MDInstrAddr { IA.addr + 1 };
}

unsigned IndirectBPU::getSetIndex(MDInstrAddr IA) const {
    return IA.addr % numSets;
}
} // namespace mcad
} // namespace llvm
