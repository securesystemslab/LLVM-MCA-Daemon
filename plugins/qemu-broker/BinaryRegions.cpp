#include "BinaryRegions.h"
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

  // Parse the manifest
  json::Object *TopLevel = JsonOrErr->getAsObject();
  if (!TopLevel)
    return llvm::make_error<json::ParseError>("Expecting a top level object",
                                              0, 0, 0);
  auto BinFilePath = TopLevel->getString("file");
  if (!BinFilePath)
    return llvm::make_error<json::ParseError>(
      "Expecting a 'file' field", 0, 0, 0);
  json::Array *RawRegions = TopLevel->getArray("regions");
  if (!RawRegions)
    return llvm::make_error<json::ParseError>(
      "Expecting a 'regions' field", 0, 0, 0);

  // We cannot use std::make_unique here because the
  // default ctor, which is declared private, will be called
  // inside an external function
  std::unique_ptr<BinaryRegions> This(new BinaryRegions());

  // Read the target object file
  auto ErrOrObjectBuffer = MemoryBuffer::getFile(*BinFilePath);
  if (!ErrOrObjectBuffer)
    return llvm::errorCodeToError(ErrOrObjectBuffer.getError());

  StringMap<BRSymbol> AllSymbols;
  auto E = This->readSymbols(*ErrOrObjectBuffer->get(), AllSymbols);
  if (E)
    return std::move(E);

  // Populate all the regions we're interested in
  E = This->parseRegions(*RawRegions, AllSymbols);
  if (E)
    return std::move(E);

  return std::move(This);
}

Error BinaryRegions::readSymbols(const MemoryBuffer &RawObjFile,
                                 StringMap<BRSymbol> &Symbols) {
  using namespace object;

  auto BinaryOrErr = llvm::object::createBinary(RawObjFile);
  if (!BinaryOrErr)
    return BinaryOrErr.takeError();
  auto &TheBinary = *BinaryOrErr;

  auto *ELFObj = dyn_cast<ELFObjectFileBase>(TheBinary.get());
  if (!ELFObj)
    return llvm::createStringError(std::errc::invalid_argument,
                                   "Unsupported binary format. "
                                   "Only ELF is supported right now");

  for (const ELFSymbolRef &Sym : ELFObj->symbols()) {
    auto Size = Sym.getSize();
    // We really don't care the error message if any of the
    // following fail, so just convert it to Optional to drop
    // the attached Error.
    auto MaybeName = llvm::expectedToOptional(Sym.getName());
    auto MaybeAddr = llvm::expectedToOptional(Sym.getAddress());
    if (MaybeName && MaybeAddr)
      Symbols[*MaybeName] = {*MaybeAddr, Size};
  }

  return llvm::ErrorSuccess();
}

Error BinaryRegions::parseRegions(json::Array &RawRegions,
                                  const StringMap<BRSymbol> &Symbols) {
  for (const json::Value &RawRegion : RawRegions) {
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

