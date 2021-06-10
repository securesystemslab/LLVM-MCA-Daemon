#include "BinaryRegions.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/WithColor.h"
#include <system_error>

using namespace llvm;
using namespace mcad;
using namespace qemu_broker;

#define DEBUG_TYPE "mcad-qemu-broker"

Expected<std::unique_ptr<BinaryRegions>>
BinaryRegions::Create(StringRef ManifestPath) {
  auto ErrOrBuffer = MemoryBuffer::getFileOrSTDIN(ManifestPath,
                                                  /*IsText=*/true);
  if (!ErrOrBuffer)
    return llvm::errorCodeToError(ErrOrBuffer.getError());
  std::unique_ptr<MemoryBuffer> &ManifestBuffer = *ErrOrBuffer;

  auto JsonOrErr = json::parse(ManifestBuffer->getBuffer());
  if (!JsonOrErr)
    return JsonOrErr.takeError();

  // We cannot use std::make_unique here because the
  // default ctor, which is declared private, will be called
  // inside an external function
  std::unique_ptr<BinaryRegions> This(new BinaryRegions());

  // Parse the manifest
  const json::Value &TopLevel = *JsonOrErr;
  // Pick the correct kind of manifest
  if (const json::Object *Obj = TopLevel.getAsObject()) {
    if (Obj->getString("file") && Obj->getArray("regions")) {
      if (auto E = This->parseSymbolBasedRegions(*Obj))
        return std::move(E);

      return std::move(This);
    }
  } else if (const json::Array *RawRegions = TopLevel.getAsArray()) {
    if (auto E = This->parseAddressBasedRegions(*RawRegions))
      return std::move(E);

    return std::move(This);
  }

  return llvm::make_error<json::ParseError>(
    "Unrecognized manifest format", 0, 0, 0);
}

struct BRSymbol {
  uint64_t StartAddr;
  size_t Size;
};

static Error readDWARF(const object::ELFObjectFileBase &ELFObj,
                       StringMap<BRSymbol> &Symbols) {
  std::unique_ptr<DWARFContext> DICtx = DWARFContext::create(ELFObj);
  for (const std::unique_ptr<DWARFUnit> &CU : DICtx->compile_units()) {
    if (!CU)
      continue;
    DWARFDie CUDie = CU->getUnitDIE(false);
    for (const DWARFDie &ChildDie : CUDie.children()) {
      if (!ChildDie.isSubprogramDIE())
        continue;
      const char *SymName = ChildDie.getLinkageName();
      if (!SymName)
        continue;

      uint64_t LowPC, HighPC, SectionIdx;
      if (!ChildDie.getLowAndHighPC(LowPC, HighPC, SectionIdx))
        continue;
      assert(HighPC >= LowPC);
      Symbols[StringRef(SymName)] = {LowPC, HighPC - LowPC};
    }
  }
  return llvm::ErrorSuccess();
}

static Error readSymTable(const object::ELFObjectFileBase &ELFObj,
                          StringMap<BRSymbol> &Symbols) {
  using namespace object;
  for (const ELFSymbolRef &Sym : ELFObj.symbols()) {
    auto Size = Sym.getSize();
    // We really don't care the error message if any of the
    // following fail, so just convert it to Optional to drop
    // the attached Error.
    auto MaybeName = llvm::expectedToOptional(Sym.getName());
    auto MaybeAddr = llvm::expectedToOptional(Sym.getAddress());
    if (MaybeName && MaybeAddr) {
      if (!Symbols.count(*MaybeName))
        Symbols[*MaybeName] = {*MaybeAddr, Size};
    }
  }

  return llvm::ErrorSuccess();
}

Error BinaryRegions::parseSymbolBasedRegions(const json::Object &RawManifest) {
  auto BinFilePath = RawManifest.getString("file");
  assert(BinFilePath && "Expecting a 'file' field");
  const json::Array *RawRegions = RawManifest.getArray("regions");
  assert(RawRegions && "Expecting a 'regions' field");

  auto ErrOrObjectBuffer = MemoryBuffer::getFile(*BinFilePath);
  if (!ErrOrObjectBuffer)
    return llvm::errorCodeToError(ErrOrObjectBuffer.getError());

  auto BinaryOrErr = llvm::object::createBinary(*ErrOrObjectBuffer->get());
  if (!BinaryOrErr)
    return BinaryOrErr.takeError();
  const object::Binary *TheBinary = (*BinaryOrErr).get();
  const auto *ELFObj = dyn_cast<object::ELFObjectFileBase>(TheBinary);
  if (!ELFObj)
    return llvm::createStringError(std::errc::invalid_argument,
                                   "Unsupported binary format. "
                                   "Only ELF is supported right now");

  StringMap<BRSymbol> Symbols;
  // Try to read DWARF first
  auto E = readDWARF(*ELFObj, Symbols);
  if (E)
    return std::move(E);
  // Then pick up symbols that are not available in DWARF
  E = readSymTable(*ELFObj, Symbols);
  if (E)
    return std::move(E);

  for (const json::Value &RawRegion : *RawRegions) {
    if (const auto *Region = RawRegion.getAsObject()) {
      auto MaybeSymName = Region->getString("symbol");
      if (!MaybeSymName)
        continue;
      if (!Symbols.count(*MaybeSymName)) {
        WithColor::warning() << "Symbol " << *MaybeSymName << " not found\n";
        continue;
      }
      const BRSymbol &BRS = Symbols.lookup(*MaybeSymName);

      // Verbose description
      StringRef Description = *MaybeSymName;
      if (auto MaybeDescription = Region->getString("description"))
        Description = *MaybeDescription;

      // Offsets
      int64_t StartOffset = 0, EndOffset = 0;
      if (const auto *OffsetsPtr = Region->getArray("offsets")) {
        const auto &Offsets = *OffsetsPtr;
        // Start offset
        if (Offsets.size() > 0) {
          if (auto MaybeStartOffset = Offsets[0].getAsInteger()) {
            // Start offset can not be negative
            if (*MaybeStartOffset >= 0)
              StartOffset = *MaybeStartOffset;
          }
        }

        // End offset
        if (Offsets.size() > 1) {
          if (auto MaybeEndOffset = Offsets[1].getAsInteger()) {
            EndOffset = *MaybeEndOffset;
          }
        }
      }

      auto StartAddr = int64_t(BRS.StartAddr);
      BinaryRegion NewBR{Description.str(),
                         // TODO: Check over/underflow
                         uint64_t(StartAddr + StartOffset),
                         uint64_t(StartAddr + int64_t(BRS.Size) + EndOffset)};
      if (!Regions.emplace(std::make_pair(NewBR.StartAddr, NewBR)).second) {
        WithColor::error() << "Entry for symbol '" << *MaybeSymName
                           << "' already exist\n";
        continue;
      }
      LLVM_DEBUG(dbgs() << "Found region for symbol '" << *MaybeSymName << "': "
                         << NewBR << "\n");
    }
  }
  return llvm::ErrorSuccess();
}

static
Optional<int64_t> parseInteger(const json::Value *Val) {
  if (!Val)
    return llvm::None;

  if (auto MaybeInt = Val->getAsInteger())
    return *MaybeInt;
  if (auto MaybeStr = Val->getAsString()) {
    int64_t Res;
    if (!MaybeStr->getAsInteger(0, Res))
      return Res;
  }
  return llvm::None;
}

Error BinaryRegions::parseAddressBasedRegions(const json::Array &RawRegions) {
  for (const json::Value &RawRegion : RawRegions) {
    const json::Object *Region = RawRegion.getAsObject();
    if (!Region)
      continue;

    auto MaybeStartAddr = parseInteger(Region->get("start")),
         MaybeEndAddr = parseInteger(Region->get("end"));
    if ((!MaybeStartAddr || !MaybeEndAddr) ||
        (*MaybeStartAddr < 0 || *MaybeEndAddr < 0))
      continue;

    StringRef Description;
    if (auto MaybeDescription = Region->getString("description"))
      Description = *MaybeDescription;

    BinaryRegion NewBR{Description.str(),
                       uint64_t(*MaybeStartAddr),
                       uint64_t(*MaybeEndAddr)};
    if (!Regions.emplace(std::make_pair(NewBR.StartAddr, NewBR)).second)
      WithColor::error() << "Entry for starting address "
                         << format_hex(NewBR.StartAddr, 16)
                         << " already exist\n";
  }
  return llvm::ErrorSuccess();
}
