//===--------------------- TimelineView.cpp ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \brief
///
/// This file implements the TimelineView interface.
///
//===----------------------------------------------------------------------===//

#include "MDCategories.h"
#include "RegionMarker.h"
#include "TimelineView.h"
#include <numeric>

namespace llvm {
namespace mca {

TimelineView::TimelineView(const MCSubtargetInfo &sti, MCInstPrinter &Printer,
                           mca::MetadataRegistry &MDR,
                           llvm::raw_ostream &OS,
                           llvm::function_ref<GetInstCallbackTy> CB)
    : InstructionView(sti, Printer, CB), MDRegistry(MDR),
      OutStream(OS), CurrentCycle(0) {}

void TimelineView::onEvent(const HWInstructionEvent &Event) {
  const unsigned Index = Event.IR.getSourceIndex();
  const Instruction &Inst = *Event.IR.getInstruction();

  bool ToBeTerminated = false;
  if (auto MDTok = Inst.getMetadataToken()) {
    const auto &MarkerCat = MDRegistry[mcad::MD_BinaryRegionMarkers];
    if (auto Marker = MarkerCat.get<mcad::RegionMarker>(*MDTok)) {
      if (Marker->isBegin() && Event.Type == HWInstructionEvent::Dispatched) {
        // Starting a new region
        PairingStack.push_back(Index);
        ActiveRegions.insert(RegionRange::createOpen(Index));
      }

      if (Marker->isEnd()) {
        if (Event.Type == HWInstructionEvent::Dispatched) {
          // Try to match a begin index
          assert(PairingStack.size() && "Begin / End mark mismatch");
          unsigned BeginIndex = PairingStack.pop_back_val();
          // Remove the open range in active regions
          auto BeginPoint = RegionRange::createPoint(BeginIndex);
          assert(ActiveRegions.count(BeginPoint)
                 && "Begin index not in active regions?");
          auto Regions = ActiveRegions.equal_range(BeginPoint);
          auto It = Regions.first, ItEnd = Regions.second;
          for (; It != ItEnd; ++It) {
            if (It->First == BeginIndex && !It->Last)
              break;
          }
          assert(It != ItEnd);
          ActiveRegions.erase(It);
          ActiveRegions.insert(RegionRange{BeginIndex, Index});
        } else if (Event.Type == HWInstructionEvent::Retired) {
          // Ready to clean up its corresponding instruction entry
          // and print
          // Note: This is faster than enumerating all active regions
          // to see if any region is terminating soon.
          ToBeTerminated = true;
        }
      }
    }
  }

  if (!ActiveRegions.count(RegionRange::createPoint(Index))) {
    assert(!ToBeTerminated);
    return;
  }

  if (!Timeline.count(Index)) {
    assert(!ToBeTerminated && "Terminating instruction not recorded?");
    Timeline.insert(std::make_pair(Index, TimelineViewEntry{-1, 0, 0, 0, 0}));
  }

  switch (Event.Type) {
  case HWInstructionEvent::Retired: {
    TimelineViewEntry &TVEntry = Timeline[Index];
    TVEntry.CycleRetired = CurrentCycle;

    // Update the WaitTime entry which corresponds to this Index.
    assert(TVEntry.CycleDispatched >= 0 && "Invalid TVEntry found!");
    unsigned CycleDispatched = static_cast<unsigned>(TVEntry.CycleDispatched);
    assert(CycleDispatched <= TVEntry.CycleReady &&
           "Instruction cannot be ready if it hasn't been dispatched yet!");
    break;
  }
  case HWInstructionEvent::Ready:
    Timeline[Index].CycleReady = CurrentCycle;
    break;
  case HWInstructionEvent::Issued:
    Timeline[Index].CycleIssued = CurrentCycle;
    break;
  case HWInstructionEvent::Executed:
    Timeline[Index].CycleExecuted = CurrentCycle;
    break;
  case HWInstructionEvent::Dispatched:
    // There may be multiple dispatch events. Microcoded instructions that are
    // expanded into multiple uOps may require multiple dispatch cycles. Here,
    // we want to capture the first dispatch cycle.
    if (Timeline[Index].CycleDispatched == -1)
      Timeline[Index].CycleDispatched = static_cast<int>(CurrentCycle);
    break;
  default:
    return;
  }

  if (ToBeTerminated) {
    auto Regions = ActiveRegions.equal_range(RegionRange::createPoint(Index));
    auto It = Regions.first, ItEnd = Regions.second;
    for (; It != ItEnd; ++It) {
      if (It->Last && *It->Last == Index)
        break;
    }
    assert(It != ItEnd);
    printTimeline(It->First, *It->Last);
    for (unsigned i = It->First, End = *It->Last + 1; i != End; ++i)
      Timeline.erase(i);
    ActiveRegions.erase(It);
  }
}

void TimelineView::printTimelineViewEntry(formatted_raw_ostream &OS,
                                          const TimelineViewEntry &Entry,
                                          unsigned SourceIndex,
                                          unsigned FirstCycle,
                                          unsigned LastCycle) const {
  if (SourceIndex == 0)
    OS << '\n';
  OS << '[' << SourceIndex << ']';
  OS.PadToColumn(10);
  assert(Entry.CycleDispatched >= 0 && "Invalid TimelineViewEntry!");
  unsigned CycleDispatched = static_cast<unsigned>(Entry.CycleDispatched);
  for (unsigned I = FirstCycle, E = CycleDispatched; I < E; ++I)
    OS << (((I - FirstCycle) % 5 == 0) ? '.' : ' ');
  OS << TimelineView::DisplayChar::Dispatched;
  if (CycleDispatched != Entry.CycleExecuted) {
    // Zero latency instructions have the same value for CycleDispatched,
    // CycleIssued and CycleExecuted.
    for (unsigned I = CycleDispatched + 1, E = Entry.CycleIssued; I < E; ++I)
      OS << TimelineView::DisplayChar::Waiting;
    if (Entry.CycleIssued == Entry.CycleExecuted)
      OS << TimelineView::DisplayChar::DisplayChar::Executed;
    else {
      if (CycleDispatched != Entry.CycleIssued)
        OS << TimelineView::DisplayChar::Executing;
      for (unsigned I = Entry.CycleIssued + 1, E = Entry.CycleExecuted; I < E;
           ++I)
        OS << TimelineView::DisplayChar::Executing;
      OS << TimelineView::DisplayChar::Executed;
    }
  }

  for (unsigned I = Entry.CycleExecuted + 1, E = Entry.CycleRetired; I < E; ++I)
    OS << TimelineView::DisplayChar::RetireLag;
  if (Entry.CycleExecuted < Entry.CycleRetired)
    OS << TimelineView::DisplayChar::Retired;

  // Skip other columns.
  unsigned EndCycle = Entry.CycleRetired + 1;
  for (unsigned I = EndCycle, E = LastCycle; I <= E; ++I)
    OS << (((I - EndCycle) % 5 == 0 || I == LastCycle) ? '.' : ' ');
}

static void printTimelineHeader(formatted_raw_ostream &OS, unsigned Cycles) {
  OS << "\n\nTimeline view:\n";
  if (Cycles >= 10) {
    OS.PadToColumn(10);
    for (unsigned I = 0; I <= Cycles; ++I) {
      if (((I / 10) & 1) == 0)
        OS << ' ';
      else
        OS << I % 10;
    }
    OS << '\n';
  }

  OS << "Index";
  OS.PadToColumn(10);
  for (unsigned I = 0; I <= Cycles; ++I) {
    if (((I / 10) & 1) == 0)
      OS << I % 10;
    else
      OS << ' ';
  }
  OS << '\n';
}

void TimelineView::printTimeline(unsigned First, unsigned Last) const {
  assert(Last >= First);
  formatted_raw_ostream FOS(OutStream);

  const auto &FirstEntry = Timeline.lookup(First);
  int FirstCycle = FirstEntry.CycleDispatched;
  assert(FirstCycle >= 0);
  FOS << "\nFirst Cycle: " << FirstCycle;
  unsigned TotalCycles
    = CurrentCycle - static_cast<unsigned>(FirstCycle);
  printTimelineHeader(FOS, TotalCycles);
  FOS.flush();

  unsigned LastCycle = Timeline.lookup(Last).CycleRetired;
  assert(LastCycle && "Last instruction not retired?");
  for (unsigned i = First, E = Last + 1; i != E; ++i) {
    const TimelineViewEntry &Entry = Timeline.lookup(i);
    if (Entry.CycleRetired == 0)
      return;

    printTimelineViewEntry(FOS, Entry, i - First,
                           static_cast<unsigned>(FirstCycle), LastCycle);
    if (const auto *Inst = getSourceInst(i))
      FOS << "   " << printInstructionString(*Inst);

    FOS << "\n";
    FOS.flush();
  }
}
} // namespace mca
} // namespace llvm
