#ifndef LLVM_MCAD_BROKERS_BROKER_H
#define LLVM_MCAD_BROKERS_BROKER_H
#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCInst.h"

namespace llvm {
namespace mcad {
// A simple interface for MCAWorker to fetch next MCInst
//
// Currently it's totally up to the Brokers to control their
// lifecycle. Client of this interface only cares about MCInsts.
struct Broker {
  // Broker should owns the MCInst so only return the pointer
  //
  // Return NULL if there is no MCInst left
  virtual const MCInst *fetch() { return nullptr; };

  // Fetch MCInsts in batch. Size is the desired number of MCInsts
  // requested by the caller. When it's -1 the Broker will put until MCIS
  // is full. Of course, it's totally possible that Broker will only give out
  // MCInsts that are less than Size.
  // Note that for performance reason we're using mutable ArrayRef so the caller
  // should supply a fixed size array. And the Broker will always write from
  // index 0.
  // Return the number of MCInst put into the buffer, or -1 if no MCInst left
  virtual int fetch(MutableArrayRef<const MCInst*> MCIS, int Size = -1) {
    return -1;
  }

  virtual ~Broker() {}
};
} // end namespace mcad
} // end namespace llvm
#endif
