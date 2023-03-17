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

    int fetch(MutableArrayRef<const MCInst *> MCIS, int Size,
              Optional<MDExchanger> MDE) override
    {
        return fetchRegion(MCIS, Size, MDE).first;
    }

    std::pair<int, RegionDescriptor>
    fetchRegion(MutableArrayRef<const MCInst *> MCIS, int Size = -1,
                Optional<MDExchanger> MDE = llvm::None) override
    {
        // Instantiate the client. It requires a channel, out of which the actual RPCs
        // are created. This channel models a connection to an endpoint specified by
        // the argument "--target=" which is the only expected argument.
        // We indicate that the channel isn't authenticated (use of
        // InsecureChannelCredentials()).
        std::string target_str = "localhost:50051";

        EmulatorClient emulator(
            grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
        std::string user("world");
        auto reply = emulator.RunInstructions(user);

        mcis = {};

        if (reply)
        {
            for (int i = 0; i < reply->instructions_size(); i++)
            {
                auto inst = reply->instructions(i).instruction();
                SmallVector<uint8_t, 1024> instructionBuffer;
                std::cout << "instruction: ";
                for (uint8_t c : inst)
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

                // Disassembled
                std::cout << std::endl
                          << "status: " << Disassembled << std::endl;
            }
            std::cout << "Client received: " << reply->DebugString() << std::endl;
            std::cout << "Is thumb: " << mcSubtargetInfo.getTargetTriple().isThumb();
            // std::cout << "Yo " << Size << std::endl;
        }

        return std::make_pair(reply->instructions_size() == 0 ? -1 : reply->instructions_size(), RegionDescriptor(false));
    }

public:
    VivisectBroker(const MCSubtargetInfo &mcSubtargetInfo,
                   MCContext &mcCtx, const Target &target) : target(target),
                                                             mcCtx(mcCtx), mcSubtargetInfo(mcSubtargetInfo)
    {

        std::cout << "STI: " << mcSubtargetInfo.getTargetTriple().normalize() << std::endl;
        std::cout << "Target: " << target.getName() << std::endl;

        disasm.reset(target.createMCDisassembler(mcSubtargetInfo, mcCtx));
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