//===--------------------- SummaryView.cpp -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the functionalities used by the SummaryView to print
/// the report information.
///
//===----------------------------------------------------------------------===//

#include "SummaryView.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MCA/Support.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "MetadataCategories.h"
#include "MetadataRegistry.h"
#include "RegionMarker.h"

namespace llvm {
namespace mca {

#define DEBUG_TYPE "llvm-mca"

SummaryView::SummaryView(const MCSchedModel &Model,
                         function_ref<size_t(void)> GetSrcSize, unsigned Width,
                         mcad::MetadataRegistry *MDRegistry,
                         llvm::raw_ostream *OutStream)
    : SM(Model), GetSourceSize(GetSrcSize),
      DispatchWidth(Width ? Width : Model.IssueWidth), LastInstructionIdx(0),
      TotalCycles(0), NumMicroOps(0), MDRegistry(MDRegistry),
      OutStream(OutStream),
      ProcResourceUsage(Model.getNumProcResourceKinds(), 0),
      ProcResourceMasks(Model.getNumProcResourceKinds()),
      ResIdx2ProcResID(Model.getNumProcResourceKinds(), 0) {
  computeProcResourceMasks(SM, ProcResourceMasks);
  for (unsigned I = 1, E = SM.getNumProcResourceKinds(); I < E; ++I) {
    unsigned Index = getResourceStateIndex(ProcResourceMasks[I]);
    ResIdx2ProcResID[Index] = I;
  }
}

void SummaryView::onEvent(const HWInstructionEvent &Event) {
  const Instruction &Inst = *Event.IR.getInstruction();
  if (Event.Type == HWInstructionEvent::Dispatched)
    LastInstructionIdx = Event.IR.getSourceIndex();

  if (MDRegistry && Inst.getIdentifier().has_value() && OutStream) {
    mcad::MetadataRegistry &MDR = *MDRegistry;
    unsigned MDTok = *Inst.getIdentifier();
    auto &MarkerCat = MDR[mcad::MD_BinaryRegionMarkers];
    if (auto Marker = MarkerCat.get<mcad::RegionMarker>(MDTok)) {
      unsigned InstIdx = Event.IR.getSourceIndex();
      bool IsBegin = Marker->isBegin(), IsEnd = Marker->isEnd();

      if (Event.Type == HWInstructionEvent::Retired) {
        if (IsBegin && IsEnd) {
          (*OutStream) << "====Marker==== [" << InstIdx << "]\n";
          printView(*OutStream);
        } else if (IsBegin) {
          // Normal Begin marker
          std::string BeginSummary;
          llvm::raw_string_ostream SS(BeginSummary);
          SS << "====Marker Begins==== [" << InstIdx << "]\n";
          printView(SS);
          PairingStack.push_back(std::move(BeginSummary));
        } else if (IsEnd) {
          // Normal End marker
          if (PairingStack.size())
            (*OutStream) << "\n" << PairingStack.pop_back_val();
          (*OutStream) << "====Marker Ends==== [" << InstIdx << "]\n";
          printView(*OutStream);
        }
      }
    }
  }

  // We are only interested in the "instruction retired" events generated by
  // the retire stage for instructions that are part of iteration #0.
  if (Event.Type != HWInstructionEvent::Retired ||
      Event.IR.getSourceIndex() >= GetSourceSize())
    return;

  // Update the cumulative number of resource cycles based on the processor
  // resource usage information available from the instruction descriptor. We
  // need to compute the cumulative number of resource cycles for every
  // processor resource which is consumed by an instruction of the block.
  const InstrDesc &Desc = Inst.getDesc();
  NumMicroOps += Desc.NumMicroOps;
  for (const std::pair<uint64_t, ResourceUsage> &RU : Desc.Resources) {
    if (RU.second.size()) {
      unsigned ProcResID = ResIdx2ProcResID[getResourceStateIndex(RU.first)];
      ProcResourceUsage[ProcResID] += RU.second.size();
    }
  }
}

void SummaryView::printView(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream TempStream(Buffer);
  DisplayValues DV;

  collectData(DV);
  TempStream << "Iterations:        " << DV.Iterations;
  TempStream << "\nInstructions:      " << DV.TotalInstructions;
  TempStream << "\nTotal Cycles:      " << DV.TotalCycles;
  TempStream << "\nTotal uOps:        " << DV.TotalUOps << '\n';
  TempStream << "\nDispatch Width:    " << DV.DispatchWidth;
  TempStream << "\nuOps Per Cycle:    "
             << format("%.2f", floor((DV.UOpsPerCycle * 100) + 0.5) / 100);
  TempStream << "\nIPC:               "
             << format("%.2f", floor((DV.IPC * 100) + 0.5) / 100);
  TempStream << "\nBlock RThroughput: "
             << format("%.1f", floor((DV.BlockRThroughput * 10) + 0.5) / 10)
             << '\n';
  TempStream.flush();
  OS << Buffer;
}

void SummaryView::collectData(DisplayValues &DV) const {
  DV.Instructions = GetSourceSize();
  DV.Iterations = (LastInstructionIdx / DV.Instructions) + 1;
  DV.TotalInstructions = DV.Instructions * DV.Iterations;
  DV.TotalCycles = TotalCycles;
  DV.DispatchWidth = DispatchWidth;
  DV.TotalUOps = NumMicroOps * DV.Iterations;
  DV.UOpsPerCycle = (double)DV.TotalUOps / TotalCycles;
  DV.IPC = (double)DV.TotalInstructions / TotalCycles;
  DV.BlockRThroughput = computeBlockRThroughput(SM, DispatchWidth, NumMicroOps,
                                                ProcResourceUsage);
}

json::Value SummaryView::toJSON() const {
  DisplayValues DV;
  collectData(DV);
  json::Object JO({{"Iterations", DV.Iterations},
                   {"Instructions", DV.TotalInstructions},
                   {"TotalCycles", DV.TotalCycles},
                   {"TotaluOps", DV.TotalUOps},
                   {"DispatchWidth", DV.DispatchWidth},
                   {"uOpsPerCycle", DV.UOpsPerCycle},
                   {"IPC", DV.IPC},
                   {"BlockRThroughput", DV.BlockRThroughput}});
  return JO;
}
} // namespace mca.
} // namespace llvm
