#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/MCA/MetadataCategories.h"
#include "llvm/MCA/MetadataRegistry.h"
#include "llvm/MCA/HardwareUnits/LSUnit.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/WithColor.h"

// #include "BinaryRegions.h"
#include "BrokerFacade.h"
#include "Brokers/Broker.h"
#include "Brokers/BrokerPlugin.h"
#include "MDCategories.h"
#include "RegionMarker.h"

#include <cstdio>

#include "Broker.h"

using namespace llvm;

#define DEBUG_TYPE "mcad-vivisect-broker"
using namespace mcad;

class VivisectBroker : public Broker
{
    std::unique_ptr<MCDisassembler> disasm;
    const MCSubtargetInfo &mcSubtargetInfo;
    MCContext &mcCtx;
    const Target &target;
    std::vector<std::shared_ptr<MCInst>> mcis;
    EmulatorClient emulator;

    int fetch(MutableArrayRef<const MCInst *> MCIS, int Size,
              Optional<MDExchanger> MDE) override
    {
        return fetchRegion(MCIS, Size, MDE).first;
    }

    std::pair<int, RegionDescriptor>
    fetchRegion(MutableArrayRef<const MCInst *> MCIS, int Size = -1,
                Optional<MDExchanger> MDE = llvm::None) override
    {
        if (Size < 0 || Size > MCIS.size())
            Size = MCIS.size();

        auto reply = emulator.RunInstructions(Size);

        mcis.clear();

        if (reply)
        {
            for (int i = 0; i < reply->instructions_size(); i++)
            {
                auto inst = reply->instructions(i);
                auto inst_bytes = inst.instruction();
                SmallVector<uint8_t, 1024> instructionBuffer;
                std::cout << "instruction: ";
                for (uint8_t c : inst_bytes)
                {
                    instructionBuffer.push_back(c);
                }
                ArrayRef<uint8_t> InstBytes(instructionBuffer);

                auto MCI = std::make_shared<MCInst>();
                uint64_t DisAsmSize;
                auto Disassembled = disasm->getInstruction(*MCI, DisAsmSize,
                                                           InstBytes,
                                                           0,
                                                           nulls());
                mcis.push_back(MCI);
                MCIS[i] = MCI.get();

                if (MDE && inst.has_memoryaccess()) {
                    auto MemAccess = inst.memoryaccess();
                    auto &Registry = MDE->MDRegistry;
                    auto &IndexMap = MDE->IndexMap;
                    auto &MemAccessCat = Registry[mca::MD_LSUnit_MemAccess];

                    MemAccessCat[i] = std::move(mca::MDMemoryAccess{
                                                    MemAccess.isstore(),
                                                    MemAccess.vaddr(),
                                                    MemAccess.size()});
                }

                // Disassembled
                std::cout << std::endl
                          << "status: " << Disassembled << std::endl;
            }
            std::cout << "Client received: " << reply->DebugString() << std::endl;
            std::cout << "Is thumb: " << mcSubtargetInfo.getTargetTriple().isThumb();
        }

        return std::make_pair(reply->instructions_size() == 0 ? -1 : reply->instructions_size(), RegionDescriptor(false));
    }

public:
    VivisectBroker(const MCSubtargetInfo &mcSubtargetInfo,
                   MCContext &mcCtx, const Target &target) : target(target),
                                                             mcCtx(mcCtx),
                                                             mcSubtargetInfo(mcSubtargetInfo),
                                                             emulator(EmulatorClient(
                                                                 grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials())))
    {

        std::cout << "STI: " << mcSubtargetInfo.getTargetTriple().normalize() << std::endl;
        std::cout << "Target: " << target.getName() << std::endl;

        disasm.reset(target.createMCDisassembler(mcSubtargetInfo, mcCtx));
    }

    unsigned getFeatures() const override {
        return Broker::Feature_Metadata;
    }
};

extern "C" ::llvm::mcad::BrokerPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
mcadGetBrokerPluginInfo()
{
    return {
        LLVM_MCAD_BROKER_PLUGIN_API_VERSION, "VivisectBroker", "v0.1",
        [](int argc, const char *const *argv, BrokerFacade &BF)
        {
            // TODO: set things like target
            BF.setBroker(std::make_unique<VivisectBroker>(
                BF.getSTI(), BF.getCtx(),
                BF.getTarget()));
        }};
}
