//===----------------------- View.cpp ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the member functions of the class InstructionView.
///
//===----------------------------------------------------------------------===//

#include <sstream>
#include "InstructionView.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"

namespace llvm {
namespace mca {

StringRef InstructionView::printInstructionString(const llvm::MCInst &MCI) const {
  InstructionString = "";
  MCIP.printInst(&MCI, 0, "", STI, InstrStream);
  InstrStream.flush();
  // Remove any tabs or spaces at the beginning of the instruction.
  return StringRef(InstructionString).ltrim();
}
} // namespace mca
} // namespace llvm
