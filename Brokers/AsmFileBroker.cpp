#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/WithColor.h"
#include <string>
#include <system_error>

#include "AsmFileBroker.h"

using namespace llvm;
using namespace mcad;

static cl::OptionCategory AsmFileBrokerCat("Assembly File Broker Options",
                                           "These options are belong to "
                                           "Assmebly File Broker");

static cl::opt<std::string>
  InputFilename("input-asm-file", cl::desc("The input assembly file"),
                cl::init("-"),
                cl::cat(AsmFileBrokerCat));

AsmFileBroker::AsmFileBroker(const Target &T, MCContext &Ctx,
                             const MCAsmInfo &MAI, const MCSubtargetInfo &STI,
                             const MCInstrInfo &MII, llvm::SourceMgr &SM)
    : SrcMgr(SM), CRG(T, SrcMgr, Ctx, MAI, STI, MII), Regions(nullptr),
      CurInstIdx(0U), IsInvalid(false) {
  llvm::InitializeAllAsmParsers();

  auto ErrOrBuffer = MemoryBuffer::getFileOrSTDIN(InputFilename);
  if (std::error_code EC = ErrOrBuffer.getError()) {
    WithColor::error() << InputFilename << ": " << EC.message() << '\n';
    return;
  }

  SrcMgr.AddNewSourceBuffer(std::move(*ErrOrBuffer), SMLoc());
}

void AsmFileBroker::Register(BrokerFacade BF) {
  BF.setBroker(std::make_unique<AsmFileBroker>(
      BF.getTarget(), BF.getCtx(), BF.getAsmInfo(), BF.getSTI(),
      BF.getInstrInfo(), BF.getSourceMgr()));
}

bool AsmFileBroker::parseIfNeeded() {
  if (!IsInvalid && !Regions) {
    auto RegionsOrErr = CRG.parseCodeRegions();
    if (!RegionsOrErr) {
      handleAllErrors(RegionsOrErr.takeError(),
                      [](const ErrorInfoBase &E) {
                        E.log(WithColor::error());
                        errs() << "\n";
                      });
      IsInvalid = true;
    } else {
      Regions = &*RegionsOrErr;
      IterRegion = Regions->begin();
      IterRegionEnd = Regions->end();
    }
  }
  return !IsInvalid;
}

const MCInst *AsmFileBroker::fetch() {
  if(!parseIfNeeded() ||
     IterRegion == IterRegionEnd) return nullptr;

  ArrayRef<MCInst> Insts = (*IterRegion)->getInstructions();
  while (CurInstIdx >= Insts.size()) {
    // Switch to next region if feasible
    if (++IterRegion == IterRegionEnd) return nullptr;
    Insts = (*IterRegion)->getInstructions();
    CurInstIdx = 0U;
  }

  return &Insts[CurInstIdx++];
}

int AsmFileBroker::fetch(MutableArrayRef<const MCInst*> MCIS, int Size,
                         std::optional<MDExchanger> MDE) {
  assert(Size <= int(MCIS.size()));
  size_t MaxLen = Size < 0? MCIS.size() : Size;

  size_t i;
  for (i = 0U; i < MaxLen; ++i) {
    const MCInst *MCI = fetch();
    if (!MCI) {
      // End of stream
      if (!i)
        return -1;
      else
        break;
    }
    MCIS[i] = MCI;
  }

  return int(i);
}

std::pair<int, Broker::RegionDescriptor>
AsmFileBroker::fetchRegion(MutableArrayRef<const MCInst*> MCIS, int Size,
                           std::optional<MDExchanger> MDE) {
  assert(Size <= int(MCIS.size()));
  size_t MaxLen = Size < 0? MCIS.size() : Size;
  StringRef LastRegionDescription;

  size_t i, NumInsts = 0U;
  bool EndOfRegion = false;
  for (i = 0U; i < MaxLen; ++i) {
    const MCInst *MCI = fetch();
    if (!MCI) {
      // End of stream
      if (!i)
        return std::make_pair(-1,
                              RegionDescriptor(true, LastRegionDescription));
      else {
        EndOfRegion = true;
        break;
      }
    }
    MCIS[i] = MCI;
    if (!NumInsts) {
      assert(IterRegion != IterRegionEnd);
      NumInsts = (*IterRegion)->getInstructions().size();
      LastRegionDescription = (*IterRegion)->getDescription();
    }

    if (CurInstIdx >= NumInsts) {
      // Is going to switch to different region in the next
      // `fetch` invocation
      EndOfRegion = true;
      ++i;
      break;
    }
  }

  return std::make_pair(int(i),
                        RegionDescriptor(EndOfRegion, LastRegionDescription));
}
