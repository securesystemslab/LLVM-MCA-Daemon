
//===------------------------- BranchUnit.h -----------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// 
/// Work in progress
///
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_HARDWAREUNITS_BRANCHUNIT_H
#define LLVM_MCA_HARDWAREUNITS_BRANCHUNIT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>
#include "MetadataCategories.h"
#include "AbstractBranchPredictorUnit.h"
#include <bitset>
#include <cstdint>
#include <unordered_map>

namespace llvm {
	namespace mcad {

		// Branch Predictor implemented according to Half&Half description
		// of Intel Skylake Branch Predictor

        
		class BranchUnit {
            public:
			virtual void recordTakenBranch(unsigned long long key, uint32_t target) = 0;
			virtual void predictCond(unsigned long long key, uint32_t target) = 0;
			virtual void predictInd(unsigned long long key, uint32_t target) = 0;
		};

		class GenericBranchUnit : public BranchUnit {
			
		};

		class SkylakeBranchUnit : public AbstractBranchPredictorUnit {
			public:
			// Maps Branch History to 4-way set of branch PC  
			struct SkylakeBranchEntry {
				unsigned long long pc;
				unsigned long long useful; // decides eviction
				
                SkylakeBranchEntry(unsigned long long pc_, unsigned long long useful_) {
                    pc = pc_;
                    useful = useful_;
                }

                SkylakeBranchEntry(unsigned long long useful_) {
                    useful = useful_;
                }
                bool operator<(const SkylakeBranchEntry& other) const {
					return useful < other.useful;
				}
				unsigned long long operator+(const SkylakeBranchEntry& other) {
					return useful + other.useful;
				}

			};
		    const static uint32_t PHR_LENGTH = 93 * 2;
        	using SkylakePHR = std::bitset<PHR_LENGTH>;
			using SkylakeBranchTable = std::unordered_map<SkylakePHR, SmallVector<SkylakeBranchEntry, 4>>;		

			SkylakeBranchUnit(uint32_t penalty_);
            void printTable();
            void printSingleTable(SkylakeBranchTable& pht);
	        BranchDirection predictBranch(MDInstrAddr pc, MDInstrAddr target);
	        BranchDirection predictBranch(MDInstrAddr pc) override;
			void recordTakenBranch(MDInstrAddr pc, MDInstrAddr target);
            void recordTakenBranch(MDInstrAddr instrAddr, BranchDirection nextInstrDirection) override;
			uint32_t getMispredictionPenalty() override;
			private:
			
			// Each table records progressively further away branches
			SkylakeBranchTable base;  
			SkylakeBranchTable pht1; 
			SkylakeBranchTable pht2;  
			SkylakeBranchTable pht3;  
            SkylakePHR phr;
			uint32_t penalty;
            

			void insertTable(SkylakeBranchTable& pht, MDInstrAddr pc, SkylakePHR phr);
	        SkylakeBranchEntry* getTable(SkylakeBranchTable& pht, 
                                                    SkylakePHR phr, 
                                                    MDInstrAddr  pc, 
                                                    SkylakeBranchEntry* out); 
			void phtSetPush(SkylakeBranchTable& pht, MDInstrAddr pc, SkylakePHR phr);
	        SkylakePHR getPHTIndex(SkylakePHR phr, int hist_length);
			unsigned long long getFootprint(MDInstrAddr branchAddr, MDInstrAddr targetAddr);
			SkylakePHR updatePHR(MDInstrAddr currentAddr, MDInstrAddr targetAddr);
            

		};


	} // namespace mcad
} // namespace llvm


#endif  // LLVM_MCA_HARDWAREUNITS_BRANCHUNIT_H
