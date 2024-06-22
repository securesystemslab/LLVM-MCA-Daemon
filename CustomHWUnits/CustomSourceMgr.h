#ifndef LLVM_MCAD_CUSTOMSOURCEMGR_H
#define LLVM_MCAD_CUSTOMSOURCEMGR_H

#include "llvm/MCA/IncrementalSourceMgr.h"

namespace llvm {
namespace mcad {

class CustomSourceMgr : public mca::IncrementalSourceMgr {
  unsigned CountTillNow = 0U;
  unsigned BatchCount = 0U;

public:
  CustomSourceMgr() = default;

  unsigned getCountTillNow() const { return CountTillNow; }

  void updateNext();
  void clear();
};

} // namespace mcad
} // namespace llvm

#endif // LLVM_MCAD_CUSTOMSOURCEMGR_H
