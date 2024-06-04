//===----------------------- InstrucionView.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the main interface for Views that examine and reference
/// a sequence of machine instructions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCA_INSTRUCTIONVIEW_H
#define LLVM_TOOLS_LLVM_MCA_INSTRUCTIONVIEW_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MCA/View.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace mca {

// The base class for views that deal with individual machine instructions.
struct InstructionView : public View {
  using GetInstCallbackTy = const llvm::MCInst*(unsigned);

private:
  const llvm::MCSubtargetInfo &STI;
  llvm::MCInstPrinter &MCIP;
  llvm::function_ref<GetInstCallbackTy> GetInstCB;
  StringRef MCPU;

  mutable std::string InstructionString;
  mutable raw_string_ostream InstrStream;

public:
  void printView(llvm::raw_ostream &) const override {}
  InstructionView(const llvm::MCSubtargetInfo &STI,
                  llvm::MCInstPrinter &Printer,
                  llvm::function_ref<GetInstCallbackTy> CB = nullptr,
                  StringRef MCPU = StringRef())
      : STI(STI), MCIP(Printer), GetInstCB(CB), MCPU(MCPU),
        InstrStream(InstructionString) {}

  virtual ~InstructionView() = default;

  StringRef getNameAsString() const override {
    return "Instructions and CPU resources";
  }
  // Return a reference to a string representing a given machine instruction.
  // The result should be used or copied before the next call to
  // printInstructionString() as it will overwrite the previous result.
  StringRef printInstructionString(const llvm::MCInst &MCI) const;
  const llvm::MCSubtargetInfo &getSubTargetInfo() const { return STI; }

  llvm::MCInstPrinter &getInstPrinter() const { return MCIP; }
  const llvm::MCInst *getSourceInst(unsigned SourceIdx) const {
    if (GetInstCB)
      return GetInstCB(SourceIdx);
    else
      return nullptr;
  }
  json::Value toJSON() const override { return json::Object(); }
  virtual void printViewJSON(llvm::raw_ostream &OS) {
    json::Value JV = toJSON();
    OS << formatv("{0:2}", JV) << "\n";
  }
};
} // namespace mca
} // namespace llvm

#endif
