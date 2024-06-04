#ifndef LLVM_MCAD_BROKERFACADE_H
#define LLVM_MCAD_BROKERFACADE_H
#include <memory>

namespace llvm {
class Target;
class MCAsmInfo;
class MCContext;
class MCSubtargetInfo;
class MCInstrInfo;
class SourceMgr;

namespace mca {
class HWEventListener;
}

namespace mcad {
class Broker;
class MCAWorker;

// An interface that provides objects that might be needed
// to build a Broker. It's also the interface to register a
// Broker.
// This class is trivially-copyable
class BrokerFacade {
  friend class MCAWorker;
  MCAWorker &Worker;

  explicit BrokerFacade(MCAWorker &W) : Worker(W) {}

public:
  void setBroker(std::unique_ptr<Broker> &&B);

  void registerListener(mca::HWEventListener *EL);

  const Target &getTarget() const;

  MCContext &getCtx() const;

  const MCAsmInfo &getAsmInfo() const;

  const MCInstrInfo &getInstrInfo() const;

  const MCSubtargetInfo &getSTI() const;

  llvm::SourceMgr &getSourceMgr() const;
};
} // end namespace mcad
} // end namespace llvm
#endif
