#include "CustomSourceMgr.h"

using namespace llvm;
using namespace mcad;

void CustomSourceMgr::updateNext() {
  BatchCount += 1;
  IncrementalSourceMgr::updateNext();
}

void CustomSourceMgr::clear() {
  CountTillNow += BatchCount;
  BatchCount = 0U;
  IncrementalSourceMgr::clear();
}
