//===--------------------- PipelinePrinter.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements class PipelinePrinter.
///
/// PipelinePrinter allows the customization of the performance report.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCA_PIPELINEPRINTER_H
#define LLVM_TOOLS_LLVM_MCA_PIPELINEPRINTER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MCA/Pipeline.h"
#include "llvm/MCA/View.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace mca {

/// A printer class that knows how to collects statistics on the
/// code analyzed by the llvm-mca tool.
///
/// This class knows how to print out the analysis information collected
/// during the execution of the code. Internally, it delegates to other
/// classes the task of printing out timeline information as well as
/// resource pressure.
class PipelinePrinter {
  Pipeline &P;
  llvm::SmallVector<std::unique_ptr<View>, 8> Views;

public:
  PipelinePrinter(Pipeline &pipeline) : P(pipeline) {}

  void addView(std::unique_ptr<View> V) {
    P.addEventListener(V.get());
    Views.emplace_back(std::move(V));
  }

  void printReport(llvm::raw_ostream &OS) const;
};
} // namespace mca
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_MCA_PIPELINEPRINTER_H
