//===--------------------- PipelinePrinter.cpp ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the PipelinePrinter interface.
///
//===----------------------------------------------------------------------===//

#include "PipelinePrinter.h"
#include "llvm/MCA/View.h"

namespace llvm {
namespace mca {

void PipelinePrinter::printReport(llvm::raw_ostream &OS) const {
  for (const auto &V : Views)
    V->printView(OS); // TODO: choose whether to print json or normal
}
} // namespace mca.
} // namespace llvm
